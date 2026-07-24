#include "video/ula.h"
#include "video/palette.h"
#include "memory/mmu.h"
#include "memory/ram.h"
#include "core/log.h"
#include "core/saveable.h"

#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// G102 — Std-ULA encoder helpers (VHDL zxula.vhd:543-553).
//
// The standard ULA encoder produces an 8-bit `ula_pixel`:
//   ula_pixel[7:5] = "000"
//   ula_pixel[4]   = NOT pixel_en        (1 = paper cycle, 0 = ink cycle)
//   ula_pixel[3]   = attr(6)              (BRIGHT bit)
//   ula_pixel[2:0] = pixel_en ? attr(2:0) : attr(5:3)
//                                          (ink/paper colour bits)
// Range 0..0x1F.  These helpers compute the encoder output for the two
// pixel-cycle phases of an 8-pixel cell.
// ---------------------------------------------------------------------------

static inline uint8_t std_ula_ink_pixel(uint8_t attr)
{
    // pixel_en=1: bit4=0, bit3=attr(6) (BRIGHT), bits 2:0=attr(2:0).
    return static_cast<uint8_t>(((attr & 0x40) >> 3) | (attr & 0x07));
}

static inline uint8_t std_ula_paper_pixel(uint8_t attr)
{
    // pixel_en=0: bit4=1, bit3=attr(6), bits 2:0=attr(5:3).
    return static_cast<uint8_t>(0x10
                              | ((attr & 0x40) >> 3)
                              | ((attr >> 3) & 0x07));
}

// ---------------------------------------------------------------------------
// ULA colour lookup — uses PaletteManager if wired, else opaque black.
//
// G102 — `ula_pixel` is the FULL 8-bit value emitted by any of the std/
// ULAnext/ULA+ encoders.  Indexes the single 256-entry × 2-bank ULA
// palette directly.  No narrower colour-table fallback — production
// code always has `palette_` set; the null fallback is just defensive
// for orphan tests.
// ---------------------------------------------------------------------------

inline uint32_t Ula::lookup_colour(uint8_t ula_pixel) const
{
    if (palette_)
        return palette_->ula_colour(active_ula_palette_, ula_pixel);
    return 0xFF000000u;
}

// ---------------------------------------------------------------------------
// vram_read — direct physical bank 5/7 access, bypassing MMU
// ---------------------------------------------------------------------------
//
// On real hardware (VHDL), the ULA reads from a dedicated dual-port RAM for
// bank 5 (and bank 7 for shadow/alternate screen). The ULA address bus is
// 14 bits (0x0000-0x3FFF within the bank). The CPU address 0x4000-0x7FFF
// maps to bank 5 physically, but the ULA ignores the MMU — it always reads
// from the physical bank.
//
// Bank 5 = 16K pages 10 and 11 in physical RAM.
// Bank 7 = 16K pages 14 and 15 (for alternate screen / hi-colour attr).

uint8_t Ula::vram_read(uint16_t addr, Mmu& mmu) const
{
    if (ram_) {
        // Direct physical access, bypassing the MMU — as on real hardware.
        // addr is in CPU space; we only care about the 14-bit bank offset.
        // Normal screen  : bank 5, starting at physical page 10.
        // Shadow screen  : bank 7 — a dedicated 8K BRAM on real hardware
        //   (VHDL: ula_bank_do <= vram_bank7_do when port_7ffd_shadow='1';
        //   bank7_ram dpram2 at zxnext.vhd:6670). Served from the Mmu's
        //   dedicated buffer when wired (Emulator::init); the legacy
        //   page-14 fallback keeps standalone unit tests working.
        const uint16_t offset = addr & 0x3FFF;
        if (vram_use_bank7_) {
            if (bank7_bram_) return bank7_bram_[offset & 0x1FFF];
            return ram_->read(14u * 8192u + offset);
        }
        // Normal screen: bank 5 — a dedicated 16K dual-port VRAM on real
        // hardware (VHDL bank5_ram dpram2 zxnext.vhd:6558-6578; ULA
        // port-B fetch :6633-6661). Served from the Mmu's buffer when
        // wired (Next machines); the legacy page-10 fallback keeps
        // standalone machines and unit tests working. Task 25.
        if (bank5_vram_) return bank5_vram_[offset];
        return ram_->read(10u * 8192u + offset);
    }
    // Fallback: read through MMU (used when RAM not wired, e.g. early tests).
    return mmu.read(addr);
}

// ---------------------------------------------------------------------------
// attr_vram_read — G12 Nirvana-class attribute replay consumer
// ---------------------------------------------------------------------------
//
// Bank-selection bug fix (Task 8 Nirvana round 3, 2026-07-13): the
// PHYSICAL bank (5 vs 7) the mux must be read from is `vram_use_bank7_`
// — the exact same signal vram_read() uses for pixel fetches — NOT
// `alt`. VHDL zxula.vhd:191 (`screen_mode_s <= i_port_ff_reg(2 downto 0)
// when i_ula_shadow_en = '0' else "000"`) and zxnext.vhd:6649-6656
// (`ula_bank_do <= vram_bank5_do1 when ula_vram_shadow = '0' else
// vram_bank7_do`) both establish that bank choice is driven PURELY by
// the 7FFD shadow-screen bit, independent of Timex screen mode — Timex
// mode only ever picks the ADDRESS OFFSET within whichever bank shadow
// has already selected, and is forced to STANDARD (clearing the
// alt-file bit) whenever shadow is asserted (see
// set_shadow_screen_en()). A previous version derived the bank choice
// from `alt` (attr_row_base >= 0x7800, i.e. Timex STANDARD_1 mode) —
// but STANDARD_1 can never be active while shadow is on (shadow forces
// STANDARD), so that derivation could NEVER select bank 7 while shadow
// was enabled — reading bank 5's (functionally unrelated) attribute
// content instead of bank 7's, for any program that enables shadow-
// screen without ever touching Timex mode (e.g. beast.nex). Confirmed
// via a scratch diagnostic (temporarily compared armed vs. direct read
// on every call) and by tracing Mmu::write()'s detector, which already
// keys the write side purely on physical page (0x0A/0x0E) — the write
// side was never the bug; only this read-side bank derivation was.
//
// `alt` still matters for a SEPARATE axis: it selects which 768-byte
// sub-window of the bank AttributeMux tracks. AttributeMux only ever
// observes writes to physical-page offset 0x1800-0x1AFF (kAttrMuxOffLo
// in mmu.h) — the "primary" attribute sub-region, which is what CPU
// address 0x5800-based (`alt`=false) addressing maps to, for EITHER
// bank. Genuine Timex STANDARD_1 addressing (`alt`=true, CPU
// 0x7800-based) lands in bank 5's UPPER 8K page (0x0B) — a page
// Mmu::write()'s detector does not watch at all (STANDARD_1 cannot
// coexist with shadow, so it is never the beast.nex/nirvana scenario) —
// so the mux must never be consulted for that range; fall through to
// the direct read exactly as pre-G12, matching what the (never-armed,
// for this address range) mux would have produced anyway.
uint8_t Ula::attr_vram_read(uint16_t addr, bool alt, Mmu& mmu) const
{
    if (!alt) {
        const uint16_t off = static_cast<uint16_t>(addr - 0x5800u);
        if (off < AttributeMux::kNumBytes) {
            const AttributeMux& mux = vram_use_bank7_ ? mmu.attr_mux7() : mmu.attr_mux5();
            // mux.started() — NOT a racing/arm heuristic (that mechanism
            // is gone; see mmu.h). A plain lifecycle guard: `current()`
            // only reflects real content once Mmu::attr_mux_start_frame()
            // has actually snapshotted a baseline. Production rendering
            // always calls it once per frame before any CPU execution,
            // so this is unconditionally true for a running emulator;
            // it only matters for callers (most subsystem unit/
            // integration tests) that construct a bare Ula+Mmu and
            // render directly without the per-frame lifecycle, and must
            // fall through to the plain read exactly as pre-G12.
            if (mux.started()) {
                return mux.current(off);
            }
        }
    }
    return vram_read(addr, mmu);
}

// ---------------------------------------------------------------------------
// encode_ulap_pixel — ULA+ 8-bit ula_pixel index (VHDL zxula.vhd:531-541)
// ---------------------------------------------------------------------------
//
// Pure function of (pixel_en, attr, screen_mode_2). Layout:
//
//   ula_pixel(7:3) = "11" & attr(7:6) & (screen_mode_2 or not pixel_en)
//   ula_pixel(2:0) = attr(2:0) if pixel_en else attr(5:3)
//
// Rows covered by this helper:
//   S7.02 — paper encoding bit 3 = NOT pixel_en (zxula.vhd:531).
//   S7.03 — palette group 3 encoding from attr(7:6) (zxula.vhd:531).
//   S7.04 — paper path for palette group 3 (zxula.vhd:531-541).
//   S7.05 — hi-res forces bit 3 via screen_mode(2) (zxula.vhd:531).
//   S7.06 — attr(7) reinterpreted as palette-group bit when ULA+ enabled.
//
// The lower-3-bit split between ink (attr(2:0)) and paper (attr(5:3)) is
// identical to the standard-ULA encoder; the difference is the upper 5
// bits — where standard ULA encodes bright/paper as "000" & not_pixel &
// attr(6), ULA+ fixes the top two bits to "11" (addressing the upper
// quadrant of the 256-entry palette store) and uses attr(7:6) as the
// palette-group selector.
uint8_t Ula::encode_ulap_pixel(bool pixel_en, uint8_t attr, bool screen_mode_2)
{
    const uint8_t pg = static_cast<uint8_t>((attr >> 6) & 0x03);  // attr(7:6)
    const uint8_t low3 = pixel_en
        ? static_cast<uint8_t>(attr & 0x07)
        : static_cast<uint8_t>((attr >> 3) & 0x07);
    const uint8_t not_pixel_en = pixel_en ? 0u : 1u;
    const uint8_t bit3 = static_cast<uint8_t>(
        ((screen_mode_2 ? 1u : 0u) | not_pixel_en) & 0x01);
    // upper 5 bits: "11" & attr(7:6) & bit3
    const uint8_t upper5 = static_cast<uint8_t>(0x18 | (pg << 1) | bit3);
    return static_cast<uint8_t>((upper5 << 3) | low3);
}

// ---------------------------------------------------------------------------
// set_screen_mode
// ---------------------------------------------------------------------------

void Ula::set_screen_mode(uint8_t port_val)
{
    screen_mode_reg_ = port_val;

    // Per-scanline G07 change-log: append BEFORE decoding mode_, so
    // apply_changes_for_line replays through this same setter and the
    // mode_ derivation is consistent.  Driving the log from the central
    // setter (rather than from each emulator port-fan-out callsite)
    // keeps the snapshot symmetrical with all callers.  When this
    // function is invoked from apply_changes_for_line() the entry would
    // be appended to the same line again — guard via a re-entrancy flag
    // implicit in the cursor-advancement contract: apply_changes_for_line
    // does not re-call set_screen_mode (it writes the field directly).
    log_port_ff_change();

    // Mode is encoded in bits 2:0 of port 0xFF per VHDL zxula.vhd:191:
    //
    //     screen_mode_s <= i_port_ff_reg(2 downto 0)
    //                      when i_ula_shadow_en = '0' else "000";
    //
    // Bit 0 of the 3-bit mode field is the alt-file select (0 = primary
    // 0x4000 pixel base, 1 = alternate 0x6000 pixel base) — see
    // zxula.vhd:218 and the `vram_a <= screen_mode(0) & …` fetches at
    // zxula.vhd:235/245.  Bits 5:3 of port 0xFF carry the HI_RES paper
    // colour (decoded by the renderer / border path) and are NOT part of
    // the mode field.  Pre-G179 jnext used bits 5:3 for the mode (a drift
    // documented in `project_g104_closed_canonical_640.md` Issue #1) which
    // made HI_RES unreachable from real Timex programs that write the
    // mode bits to port 0xFF(2:0) per the SCLD spec.
    const uint8_t mode_bits = port_val & 0x07;
    // Wave D (S5.04) — alt-file bit tracks mode_bits(0).  Keep `alt_file_`
    // consistent with the last write to port 0xFF so that HI_COLOUR (mode 010)
    // vs. HI_COLOUR+alt (mode 011) can be distinguished by the renderer.
    alt_file_ = (mode_bits & 0x01) != 0;
    switch (mode_bits) {
        case 0:                                       // 000 = STANDARD
        case 1: mode_ = (mode_bits == 0)              // 001 = STANDARD+alt
                        ? TimexScreenMode::STANDARD
                        : TimexScreenMode::STANDARD_1;
                break;
        case 2:                                       // 010 = HI_COLOUR
        case 3: mode_ = TimexScreenMode::HI_COLOUR;   // 011 = HI_COLOUR+alt
                break;                                // (alt_file_ disambiguates)
        case 6:                                       // 110 = HI_RES
        case 7: mode_ = TimexScreenMode::HI_RES;      // 111 = HI_RES+alt
                break;                                // (alt_file_ disambiguates)
        default:
            Log::ula()->warn("Unknown Timex screen mode bits {:#04x}, defaulting to STANDARD", mode_bits);
            mode_ = TimexScreenMode::STANDARD;
            break;
    }
    Log::ula()->debug("Screen mode set: port_val={:#04x} mode={} alt_file={}",
                      port_val, static_cast<int>(mode_), alt_file_);
}

// ---------------------------------------------------------------------------
// Per-scanline port-0xFF change-log (G07)
// ---------------------------------------------------------------------------
//
// VHDL refs:
//   zxnext.vhd:3615-3616  port_ff_wr → port_ff_reg <= cpu_do
//   zxnext.vhd:3630       port_ff_dat_tmx <= port_ff_reg (CPU-clk latch)
//   zxula.vhd:191         screen_mode_s <= i_port_ff_reg(2:0)
//   zxula.vhd:209         screen_mode <= screen_mode_s (per character cell)
//
// The renderer pattern mirrors layer2 / palette / sprites: the live
// `screen_mode_reg_` holds the end-of-frame value after all writes have
// been applied; the change log captures every write tagged with the
// scanline at which it occurred.  Before per-scanline render, the live
// state is rewound to the frame baseline; for each scanline we replay
// any entries whose line tag matches and re-derive `mode_` / `alt_file_`
// using the same mode-decode path as `set_screen_mode`.

void Ula::log_port_ff_change()
{
    if (port_ff_count_ >= MAX_CHANGES_PER_FRAME) {
        if (!port_ff_overflow_warned_) {
            Log::ula()->warn(
                "Ula: port-0xFF change-log full at line {} (cap {} per "
                "frame); further port-0xFF writes this frame will not "
                "be per-scanline.",
                current_line_, MAX_CHANGES_PER_FRAME);
            port_ff_overflow_warned_ = true;
        }
        return;
    }
    port_ff_log_[port_ff_count_++] = PortFFChange{
        current_line_, screen_mode_reg_,
    };
}

void Ula::start_frame()
{
    baseline_port_ff_        = screen_mode_reg_;
    port_ff_count_           = 0;
    port_ff_render_cursor_   = 0;
    current_line_            = 0;
    port_ff_overflow_warned_ = false;
}

// Apply the screen-mode decode used by set_screen_mode without
// re-logging.  Mirrors the case-statement in set_screen_mode exactly so
// the post-replay state is identical to the live state at write time.
// Mode bits live in port_ff(2:0) per VHDL zxula.vhd:191.
static inline void decode_screen_mode_into(
    uint8_t port_val, TimexScreenMode& mode, bool& alt_file)
{
    const uint8_t mode_bits = port_val & 0x07;
    alt_file = (mode_bits & 0x01) != 0;
    switch (mode_bits) {
        case 0:
        case 1: mode = (mode_bits == 0)
                       ? TimexScreenMode::STANDARD
                       : TimexScreenMode::STANDARD_1;
                break;
        case 2:
        case 3: mode = TimexScreenMode::HI_COLOUR;
                break;
        case 6:
        case 7: mode = TimexScreenMode::HI_RES;
                break;
        default:
            mode = TimexScreenMode::STANDARD;
            break;
    }
}

void Ula::rewind_to_baseline()
{
    screen_mode_reg_ = baseline_port_ff_;
    decode_screen_mode_into(screen_mode_reg_, mode_, alt_file_);
    port_ff_render_cursor_ = 0;
}

void Ula::apply_changes_for_line(int line)
{
    const uint16_t lt = static_cast<uint16_t>(line);
    while (port_ff_render_cursor_ < port_ff_count_
        && port_ff_log_[port_ff_render_cursor_].line == lt) {
        const auto& c = port_ff_log_[port_ff_render_cursor_++];
        screen_mode_reg_ = c.value;
        decode_screen_mode_into(screen_mode_reg_, mode_, alt_file_);
    }
}

void Ula::flush_remaining_changes()
{
    // Drain port-0xFF (Timex) entries that landed in vblank. Mirrors
    // Tilemap::flush_remaining_nr6b_changes — see Renderer::render_frame
    // for rationale.
    while (port_ff_render_cursor_ < port_ff_count_) {
        const auto& c = port_ff_log_[port_ff_render_cursor_++];
        screen_mode_reg_ = c.value;
        decode_screen_mode_into(screen_mode_reg_, mode_, alt_file_);
    }
}

// ---------------------------------------------------------------------------
// pixel_addr_offset
// ---------------------------------------------------------------------------
//
// ZX Spectrum screen layout in VRAM (starting at 0x4000):
//
//   Pixel bytes:     0x4000 – 0x57FF  (6 KiB, 192 rows × 32 bytes/row)
//   Attribute bytes: 0x5800 – 0x5AFF  (768 bytes, 24 attr-rows × 32 bytes)
//
// The pixel address is NOT simply row * 32 + col.  The screen is divided into
// three 64-row thirds, and within each third the rows are stored
// interleaved by groups of 8:
//
//   pixel_addr = base
//              | ((screen_row & 0xC0) << 5)   // third select  → bits 12:11
//              | ((screen_row & 0x07) << 8)   // fine row       → bits 10:8
//              | ((screen_row & 0x38) << 2)   // coarse row     → bits 7:5
//              | col                           // column (0–31)  → bits 4:0

uint16_t Ula::pixel_addr_offset(int screen_row, int col)
{
    return static_cast<uint16_t>(
          ((screen_row & 0xC0) << 5)
        | ((screen_row & 0x07) << 8)
        | ((screen_row & 0x38) << 2)
        | col);
}

// ---------------------------------------------------------------------------
// Scroll folds (VHDL zxula.vhd:192-207 for Y, :199 for X)
// ---------------------------------------------------------------------------
//
// Y-fold (zxula.vhd:192-207):
//   py_s <= i_vc + ('0' & i_ula_scroll_y);            -- 9-bit add
//   if py_s(8 downto 7) = "11" then                   -- py_s ∈ [384, 511]
//      py <= (not py_s(7)) & py_s(6 downto 0);        -- clear bit 7 (−128)
//   elsif py_s(8) = '1' or py_s(7 downto 6) = "11" then   -- py_s ∈ [192,383]
//      py <= (py_s(7 downto 6) + 1) & py_s(5 downto 0);   -- +64 in bits(7:6),
//                                                          -- mod 4 with wrap
//   else
//      py <= py_s(7 downto 0);                        -- passthrough
//   end if;
// Functionally this is (vc + scroll_y) mod 192 with the specific encoding the
// hardware uses to preserve third-cycling in the subsequent address
// calculation `addr_p_spc_12_5 <= py(7:6) & py(2:0) & py(5:3)` (zxula.vhd:223).
static int fold_ula_y(int raw_y, uint8_t scroll_y)
{
    const int py_s = (raw_y & 0x1FF) + scroll_y;  // 9-bit result
    const int b87  = (py_s >> 7) & 0x3;
    const int b8   = (py_s >> 8) & 0x1;
    const int b76  = (py_s >> 6) & 0x3;
    if (b87 == 0x3) {
        // py_s ∈ [384, 511] : clear bit 7.
        return py_s & 0x7F;
    } else if (b8 == 1 || b76 == 0x3) {
        // py_s ∈ [192, 383] : add 64 to bits(7:6), low 6 bits pass through.
        const int new76 = (((py_s >> 6) & 0x3) + 1) & 0x3;
        return static_cast<int>((new76 << 6) | (py_s & 0x3F));
    }
    return py_s & 0xFF;
}

// X-fold (zxula.vhd:199):
//   px <= i_ula_fine_scroll_x & (i_hc(7:3) + i_ula_scroll_x(7:3)) &
//         i_ula_scroll_x(2:0);
// with px_1 <= px(8) & (px(7:3)+1) & px(2:0) (line 216) feeding the vram_a
// byte-column field and px(2:0) acting as the bit-within-byte phase. The
// observable effect at output-pixel granularity is that output pixel at
// display column X reads source pixel at
//   src_x = (X + scroll_x + fine_scroll_x) mod 256.
// NR 0x26 is an 8-bit pixel offset: bits(7:3) are byte-column shift
// (scroll_x(7:3) * 8 pixels), bits(2:0) are bit-within-byte shift. Since the
// byte-shift already multiplies by 8, the raw 8-bit value IS the pixel shift.
static int fold_ula_x(int raw_x, uint8_t scroll_x, bool fine_scroll_x)
{
    return (raw_x + static_cast<int>(scroll_x) + (fine_scroll_x ? 1 : 0)) & 0xFF;
}

// ---------------------------------------------------------------------------
// render_frame
// ---------------------------------------------------------------------------

void Ula::render_frame(uint32_t* framebuffer, Mmu& mmu)
{
    // The 640×256 output framebuffer is laid out as follows (G104 canonical):
    //
    //   Rows   0..DISP_Y-1              → top border  (DISP_Y = 32)
    //   Rows   DISP_Y..DISP_Y+DISP_H-1 → display area (192 rows)
    //   Rows   DISP_Y+DISP_H..FB_HEIGHT-1 → bottom border
    //
    // Framebuffer height is 256, giving symmetric borders:
    //   top = 32, display = 192, bottom = 256 - 32 - 192 = 32.
    // Per row: 64px left border + 512px display + 64px right border = 640.

    for (int row = 0; row < FB_HEIGHT; ++row) {
        uint32_t* line = framebuffer + row * FB_WIDTH;
        const int screen_row = row - DISP_Y;   // < 0 if in top border

        // Use per-line border colour (snapshotted during frame execution).
        border_colour_ = border_per_line_[row];

        if (screen_row >= 0 && screen_row < DISP_H) {
            switch (mode_) {
                case TimexScreenMode::STANDARD: {
                    // Primary screen: pixels at 0x4000, attrs at 0x5800.
                    const uint16_t poff     = pixel_addr_offset(screen_row, 0);
                    const uint16_t attr_row = static_cast<uint16_t>(
                        0x5800u + (screen_row / 8) * 32);
                    render_display_line(line, screen_row, poff, attr_row, mmu);
                    break;
                }
                case TimexScreenMode::STANDARD_1: {
                    // Alternate screen: pixels at 0x6000, attrs at 0x7800.
                    const uint16_t poff     = pixel_addr_offset(screen_row, 0);
                    const uint16_t attr_row = static_cast<uint16_t>(
                        0x7800u + (screen_row / 8) * 32);
                    // pixel_base_offset is used as an offset from 0x6000 in
                    // render_display_line — see that function's implementation.
                    render_display_line(line, screen_row, poff, attr_row, mmu);
                    break;
                }
                case TimexScreenMode::HI_COLOUR:
                    render_display_line_hicolour(line, screen_row, mmu);
                    break;
                case TimexScreenMode::HI_RES:
                    render_display_line_hires(line, screen_row, mmu);
                    break;
            }
        } else {
            // Top or bottom border: fill entire row with border colour.
            render_border_line(line);
        }
    }

    // Advance flash state once per frame.
    advance_flash();
}

// ---------------------------------------------------------------------------
// render_scanline — render a single row for the compositor
// ---------------------------------------------------------------------------

void Ula::render_scanline(uint32_t* dst, int row, Mmu& mmu, bool* border_dst)
{
    // Temporarily use the per-line border colour for rendering, but restore
    // the live border_colour_ afterwards so init_border_per_line() at the
    // next frame start uses the value last set by port 0xFE, not the
    // per-line snapshot from the last rendered row.
    const uint8_t saved_border = border_colour_;
    if (row >= 0 && row < FB_HEIGHT)
        border_colour_ = border_per_line_[row];

    const int screen_row = row - DISP_Y;

    if (screen_row >= 0 && screen_row < DISP_H) {
        switch (mode_) {
            case TimexScreenMode::STANDARD: {
                const uint16_t poff     = pixel_addr_offset(screen_row, 0);
                const uint16_t attr_row = static_cast<uint16_t>(
                    0x5800u + (screen_row / 8) * 32);
                render_display_line(dst, screen_row, poff, attr_row, mmu,
                                    border_dst);
                break;
            }
            case TimexScreenMode::STANDARD_1: {
                const uint16_t poff     = pixel_addr_offset(screen_row, 0);
                const uint16_t attr_row = static_cast<uint16_t>(
                    0x7800u + (screen_row / 8) * 32);
                render_display_line(dst, screen_row, poff, attr_row, mmu,
                                    border_dst);
                break;
            }
            case TimexScreenMode::HI_COLOUR:
                render_display_line_hicolour(dst, screen_row, mmu, border_dst);
                break;
            case TimexScreenMode::HI_RES:
                render_display_line_hires(dst, screen_row, mmu, border_dst);
                break;
        }
    } else {
        // Top or bottom border row — every cell is border.
        render_border_line(dst, border_dst);
    }

    border_colour_ = saved_border;
}

// ---------------------------------------------------------------------------
// render_scanline_bank — render a scanline forcing bank 5 or bank 7 (debugger)
// ---------------------------------------------------------------------------
//
// The debugger video panel has two ULA views ("Primary (bank 5)" and
// "Shadow (bank 7)") and each must show ITS bank regardless of which one the
// running program currently has selected via port 0x7FFD bit 3.  We therefore
// override `vram_use_bank7_` for the duration of the call — every vram_read()
// inside the render path then goes to the requested bank — and restore it
// afterwards, so emulation state is untouched.
//
// Everything else (Timex screen mode from port 0xFF, ULA scroll, palette
// selector, per-line border snapshot) is inherited by delegating to the live
// render_scanline(): the debug views must be pixel-identical to what the
// compositor produces for the selected bank, not a STANDARD-mode
// approximation of it.  (The previous implementation open-coded a
// STANDARD-mode-only path, so a Timex hi-colour / hi-res program's shadow
// screen was decoded with the wrong layout.)

void Ula::render_scanline_bank(uint32_t* dst, int row, Mmu& mmu, bool use_bank7)
{
    const bool saved_bank7 = vram_use_bank7_;
    vram_use_bank7_ = use_bank7;
    render_scanline(dst, row, mmu, /*border_dst=*/nullptr);
    vram_use_bank7_ = saved_bank7;
}

// ---------------------------------------------------------------------------
// advance_flash — call once per frame after rendering all scanlines
// ---------------------------------------------------------------------------

void Ula::advance_flash()
{
    ++flash_counter_;
    if (flash_counter_ >= 16) {
        flash_counter_ = 0;
        flash_phase_   = !flash_phase_;
    }
}

// ---------------------------------------------------------------------------
// compute_ulanext_pixel  (VHDL zxula.vhd:492-529)
// ---------------------------------------------------------------------------
//
// Pure encoder for the ULAnext palette-format pixel path. Consumed by the
// live render paths (G102 dispatch in render_display_line /
// render_display_line_hicolour / render_display_line_hires and the TMX
// border route) and by the Wave-B encoder unit tests. Both halves of the
// return value are live: `pixel` indexes the 256-entry ULA palette, and
// `select_bgnd` routes the pixel to the NR $4A fallback per
// zxnext.vhd:6986-6991 (LR-140 — see set_select_bgnd_argb in ula.h).
//
// VHDL paper-lookup table (paper_base_index = "10000000"):
//
//   format  paper encoding            ink width   paper output pattern
//   ------  ------------------------  ---------   --------------------
//   0x01    pbi(7)   & attr(7:1)      1-bit ink    0x80 | (attr >> 1) & 0x7F
//   0x03    pbi(7:6) & attr(7:2)      2-bit ink    0x80 | (attr >> 2) & 0x3F
//   0x07    pbi(7:5) & attr(7:3)      3-bit ink    0x80 | (attr >> 3) & 0x1F
//   0x0F    pbi(7:4) & attr(7:4)      4-bit ink    0x80 | (attr >> 4) & 0x0F
//   0x1F    pbi(7:3) & attr(7:5)      5-bit ink    0x80 | (attr >> 5) & 0x07
//   0x3F    pbi(7:2) & attr(7:6)      6-bit ink    0x80 | (attr >> 6) & 0x03
//   0x7F    pbi(7:1) & attr(7)        7-bit ink    0x80 | (attr >> 7) & 0x01
//   other   ula_select_bgnd <= '1'   (incl. 0xFF — transparent paper)

Ula::UlaNextPixel Ula::compute_ulanext_pixel(bool pixel_en, bool border,
                                             uint8_t attr) const
{
    UlaNextPixel out{0, false};
    const uint8_t format = ulanext_format_;
    constexpr uint8_t PBI = 0x80; // paper_base_index = "10000000" (zxula.vhd:486)

    if (border) {
        // zxula.vhd:496-504 — border:
        //   ula_pixel <= paper_base_index(7:3) & attr(5:3)
        //   if format = X"FF" → ula_select_bgnd <= '1'
        out.pixel = static_cast<uint8_t>((PBI & 0xF8) | ((attr >> 3) & 0x07));
        if (format == 0xFF) out.select_bgnd = true;
        return out;
    }

    if (pixel_en) {
        // zxula.vhd:510 — ink: ula_pixel <= attr AND format.
        out.pixel = static_cast<uint8_t>(attr & format);
        return out;
    }

    // pixel_en=0 → paper cycle (zxula.vhd:516-527).
    switch (format) {
        case 0x01: out.pixel = static_cast<uint8_t>(PBI | ((attr >> 1) & 0x7F)); break;
        case 0x03: out.pixel = static_cast<uint8_t>(PBI | ((attr >> 2) & 0x3F)); break;
        case 0x07: out.pixel = static_cast<uint8_t>(PBI | ((attr >> 3) & 0x1F)); break;
        case 0x0F: out.pixel = static_cast<uint8_t>(PBI | ((attr >> 4) & 0x0F)); break;
        case 0x1F: out.pixel = static_cast<uint8_t>(PBI | ((attr >> 5) & 0x07)); break;
        case 0x3F: out.pixel = static_cast<uint8_t>(PBI | ((attr >> 6) & 0x03)); break;
        case 0x7F: out.pixel = static_cast<uint8_t>(PBI | ((attr >> 7) & 0x01)); break;
        default:
            // when others (incl. 0xFF): ula_select_bgnd <= '1'; pixel undefined.
            out.select_bgnd = true;
            break;
    }
    return out;
}

// ---------------------------------------------------------------------------
// render_display_line  (STANDARD and STANDARD_1)
// ---------------------------------------------------------------------------
//
// Parameters:
//   pixel_base_offset  — interleaved row offset (from pixel_addr_offset for
//                        column 0); added to 0x4000 (STANDARD) or 0x6000
//                        (STANDARD_1).
//   attr_row_base      — base address of the 32-byte attribute row for this
//                        display line (e.g. 0x5800 + (row/8)*32 for primary,
//                        0x7800 + (row/8)*32 for alternate).
//
// The pixel_base_offset already encodes the interleaved row; column bytes are
// read at pixel_base_offset + col.  For STANDARD_1 the base is shifted to
// 0x6000 — this function receives the base as (0x4000|poff) or (0x6000|poff)
// via the caller, so we keep the same logic.
//
// NOTE: the caller in render_frame passes pixel_addr_offset(screen_row, 0)
// (column-zero offset) and the appropriate attr_row_base.  For STANDARD_1 the
// pixel_base is rebuilt here by OR-ing with 0x6000 instead of 0x4000.

void Ula::render_display_line(uint32_t* row, int screen_row,
                               uint16_t pixel_base_offset,
                               uint16_t attr_row_base,
                               Mmu& mmu,
                               bool* border_dst)
{
    // Apply the ULA Y-scroll fold per zxula.vhd:192-207. With scroll_y==0
    // this returns screen_row unchanged so the default no-scroll case is
    // unaffected. Cross-third wrap (scroll_y=64 etc.) is handled by the
    // fold because py(7:6) is re-encoded before addr_p_spc_12_5 is formed
    // (zxula.vhd:223).
    const int eff_row = fold_ula_y(screen_row, ula_scroll_y_);

    // The caller hands us pixel_base_offset = pixel_addr_offset(screen_row, 0);
    // regenerate it from the folded row so the pixel/attr addresses reflect
    // the scrolled line. If scroll_y is zero this matches the incoming value.
    const bool alt = (attr_row_base >= 0x7800);
    (void)pixel_base_offset;  // regenerated below for correctness under scroll
    const uint16_t eff_poff      = pixel_addr_offset(eff_row, 0);
    const uint16_t pixel_base    = static_cast<uint16_t>((alt ? 0x6000u : 0x4000u) | eff_poff);
    const uint16_t eff_attr_base = static_cast<uint16_t>((alt ? 0x7800u : 0x5800u)
                                                         + (eff_row / 8) * 32);

    // Fill left border pixels (DISP_X = 64 pixels under G104).
    //
    // VHDL zxula.vhd:418 — `border_clr <= "00" & port_fe & port_fe`, so
    // attr = (border<<3) | border (paper bits 5:3 = ink bits 2:0 = border
    // 0..7), attr(6)=0.  border_active_d=1 forces paper cycle through
    // the std-ULA encoder (zxula.vhd:543-553), yielding
    // ula_pixel = std_ula_paper_pixel(attr) = 0x10 | (border & 7).
    const uint8_t border_attr  = static_cast<uint8_t>(
        ((border_colour_ & 0x07) << 3) | (border_colour_ & 0x07));
    const uint32_t border_argb = lookup_colour(std_ula_paper_pixel(border_attr));
    for (int x = 0; x < DISP_X; ++x) {
        row[x] = border_argb;
        // VHDL zxula.vhd:415 — border_active_v is asserted at every left-
        // border column; ula_border_2 (zxnext.vhd:7256/7266/7278) inherits
        // it via the o_ula_border output (zxula.vhd:567).
        if (border_dst) border_dst[x] = true;
    }

    // X-scroll fold per zxula.vhd:199: source pixel for display pixel X is
    //   src_x = (X + scroll_x + fine_scroll_x) mod 256.
    // When there is no X scroll we keep the fast column-oriented loop so the
    // no-scroll default path is byte-for-byte identical to the pre-scroll
    // implementation.
    const uint8_t scroll_x = ula_scroll_x_coarse_;
    const bool    fine     = ula_fine_scroll_x_;

    // G103 — ULA+ runtime palette path.  Gate is ulap_en_ (set by port
    // 0xFF3B b0 with ulap_mode="01", or NR 0x68 b3 ungated).  Default
    // (ulap_en_=false) keeps the std-ULA encoder path against the
    // single 256-entry × 2-bank ULA palette (zxula.vhd:543-553 +
    // zxnext.vhd:6981) byte-identical.
    //
    // VHDL zxula.vhd:531-541 (encoder) + zxnext.vhd:6981 (palette read):
    //
    //   ula_pixel(7:6) = "11"            (selects ULA+ region 0xC0..0xFF)
    //   ula_pixel(5:4) = attr(7:6)       (palette group bits)
    //   ula_pixel(3)   = screen_mode(2) or not pixel_en
    //   ula_pixel(2:0) = pixel_en ? attr(2:0) : attr(5:3)
    //
    //   palette_utm address = '0' & ula_palette_select_1 & ula_pixel(7:0)
    //
    // The runtime read uses NR 0x43 b1 (`active_ula_palette_`) for bank
    // selection — distinct from NR 0x43 b6 (`palette_write_select(2)`)
    // used by NR 0xFF poke writes and by the port 0xFF3B palette read
    // (both via zxnext.vhd:6958).  `ulap_colour()` takes `ula_pixel & 0x3F`
    // and offsets it by PaletteManager::ULAP_BASE, because the 64 ULA+
    // slots are ULA palette indices 0xC0..0xFF of the one `palette_utm`
    // dpram — the same words the NR 0x41 / NR 0x44 stream writes (#64).
    const bool sm2 = (mode_ == TimexScreenMode::HI_RES);
    if (scroll_x == 0 && !fine) {
        // Fast path (unchanged semantics under default NR 0x26/NR 0x68 bit 2).
        for (int col = 0; col < 32; ++col) {
            const uint8_t pixels = vram_read(static_cast<uint16_t>(pixel_base + col), mmu);
            const uint8_t attr_raw = attr_vram_read(static_cast<uint16_t>(eff_attr_base + col), alt, mmu);

            // zxula.vhd:470 — `attr_active(7) and flash_cnt(4) and
            // (not i_ulanext_en) and not i_ulap_en`: BOTH ULAnext (Wave B)
            // and ULA+ (Wave C) suppress the flash XOR term so attr(7) can
            // be reinterpreted as a palette-group bit.  When the XOR fires,
            // it swaps ink/paper colour bits in attr_active (BRIGHT and
            // FLASH bits unchanged).
            uint8_t attr = attr_raw;
            const bool flash_a = (attr_raw & 0x80) != 0;
            if (flash_a && flash_phase_ && !ulanext_en_ && !ulap_en_) {
                const uint8_t ink_bits   = attr_raw       & 0x07;
                const uint8_t paper_bits = (attr_raw >> 3) & 0x07;
                attr = static_cast<uint8_t>(
                    (attr_raw & 0xC0) | (ink_bits << 3) | paper_bits);
            }

            uint32_t ink_argb;
            uint32_t paper_argb;
            if (ulanext_en_ && palette_) {
                // G102 — ULAnext runtime palette path.  Encoder per
                // zxula.vhd:485-528 produces an 8-bit ula_pixel; runtime
                // read into palette_utm uses the full 8-bit value as the
                // low address bits with `ula_palette_select_1` selecting
                // the bank (zxnext.vhd:6981).
                //
                // LR-140 — zxnext.vhd:6987-6991: a pixel whose encoder
                // asserted ula_select_bgnd takes the NR $4A fallback
                // (expanded per :6990) instead of the palette lookup.
                // The encoder only asserts it for paper (zxula.vhd:525)
                // and border (:501); the mux is written on both slots
                // because the VHDL applies it to the pixel stream, not
                // to the ink/paper split.
                const auto ink_pix   = compute_ulanext_pixel(
                    /*pixel_en*/true,  /*border*/false, attr);
                const auto paper_pix = compute_ulanext_pixel(
                    /*pixel_en*/false, /*border*/false, attr);
                ink_argb   = ink_pix.select_bgnd
                                 ? select_bgnd_argb_
                                 : palette_->ula_colour(active_ula_palette_,
                                                        ink_pix.pixel);
                paper_argb = paper_pix.select_bgnd
                                 ? select_bgnd_argb_
                                 : palette_->ula_colour(active_ula_palette_,
                                                        paper_pix.pixel);
            } else if (ulap_en_ && palette_) {
                // ULA+ encoder common bits per zxula.vhd:535 (only differ
                // by bit 3 = NOT pixel_en):
                //   low6 = attr(7:6) << 4 | bit3 << 3 | low3
                const uint8_t pg = static_cast<uint8_t>((attr >> 6) & 0x03);
                // ink: bit3 = sm2 (pixel_en=1 → not pixel_en=0).
                const uint8_t ink_low6 = static_cast<uint8_t>(
                    (pg << 4) | ((sm2 ? 1u : 0u) << 3) | (attr & 0x07));
                // paper: bit3 = sm2 OR 1 = 1 (always when pixel_en=0).
                const uint8_t paper_low6 = static_cast<uint8_t>(
                    (pg << 4) | (1u << 3) | ((attr >> 3) & 0x07));
                ink_argb   = palette_->ulap_colour(active_ula_palette_, ink_low6);
                paper_argb = palette_->ulap_colour(active_ula_palette_, paper_low6);
            } else {
                // G102 — std-ULA encoder per VHDL zxula.vhd:543-553 emits
                // the full 8-bit ula_pixel, which then indexes the single
                // 256-entry × 2-bank ULA palette directly (zxnext.vhd:6981).
                ink_argb   = lookup_colour(std_ula_ink_pixel(attr));
                paper_argb = lookup_colour(std_ula_paper_pixel(attr));
            }

            // VHDL zxula.vhd:390-393 — in lo-res mode (shift_screen_mode(2)='0')
            // shift_reg_32 doubles each shift_pbyte bit (`shift_pbyte(15) &
            // shift_pbyte(15) & shift_pbyte(14) & shift_pbyte(14) & ...`).
            // So each source bit emits TWO adjacent framebuffer cells; total
            // output = 16 cells per source byte = 512 cells per 32-byte row.
            uint32_t* dst = row + DISP_X + col * 16;
            for (int bit = 7; bit >= 0; --bit) {
                const uint32_t px = (pixels >> bit) & 1 ? ink_argb : paper_argb;
                *dst++ = px;
                *dst++ = px;
            }
        }
    } else {
        // Scrolled path: per-pixel source lookup. The VHDL shift-register
        // emits px(7:3) (byte column) and px(2:0) (within-byte bit phase)
        // with px(8) adding a 1-pixel fine offset (line 199 + 216). The
        // intrinsic source-bit doubling (VHDL zxula.vhd:390-393) becomes
        // observable as: each pair of output pixels reads the same source
        // bit (src_x = disp_x / 2 + scroll). Total emit = 512 cells.
        for (int disp_x = 0; disp_x < 512; ++disp_x) {
            const int src_x  = fold_ula_x(disp_x / 2, scroll_x, fine);
            const int src_col = src_x >> 3;          // byte column 0..31
            const int src_bit = 7 - (src_x & 0x7);   // MSB-first within byte
            const uint8_t pixels = vram_read(
                static_cast<uint16_t>(pixel_base + src_col), mmu);
            const uint8_t attr_raw = attr_vram_read(
                static_cast<uint16_t>(eff_attr_base + src_col), alt, mmu);

            // zxula.vhd:470 — both ULAnext (Wave B) and ULA+ (Wave C)
            // suppress the flash XOR; mirrors the fast-path gate above.
            uint8_t attr = attr_raw;
            const bool flash_a = (attr_raw & 0x80) != 0;
            if (flash_a && flash_phase_ && !ulanext_en_ && !ulap_en_) {
                const uint8_t ink_bits   = attr_raw       & 0x07;
                const uint8_t paper_bits = (attr_raw >> 3) & 0x07;
                attr = static_cast<uint8_t>(
                    (attr_raw & 0xC0) | (ink_bits << 3) | paper_bits);
            }

            uint32_t ink_argb;
            uint32_t paper_argb;
            if (ulanext_en_ && palette_) {
                // G102 — ULAnext runtime path; see fast-path for full
                // VHDL citations.  LR-140 — select_bgnd routes to the
                // NR $4A fallback (zxnext.vhd:6987-6991).
                const auto ink_pix   = compute_ulanext_pixel(
                    /*pixel_en*/true,  /*border*/false, attr);
                const auto paper_pix = compute_ulanext_pixel(
                    /*pixel_en*/false, /*border*/false, attr);
                ink_argb   = ink_pix.select_bgnd
                                 ? select_bgnd_argb_
                                 : palette_->ula_colour(active_ula_palette_,
                                                        ink_pix.pixel);
                paper_argb = paper_pix.select_bgnd
                                 ? select_bgnd_argb_
                                 : palette_->ula_colour(active_ula_palette_,
                                                        paper_pix.pixel);
            } else if (ulap_en_ && palette_) {
                // G103 — ULA+ runtime path.  Mirrors the fast-path block
                // above; see comments there for the encoder layout.
                const uint8_t pg = static_cast<uint8_t>((attr >> 6) & 0x03);
                const uint8_t ink_low6 = static_cast<uint8_t>(
                    (pg << 4) | ((sm2 ? 1u : 0u) << 3) | (attr & 0x07));
                const uint8_t paper_low6 = static_cast<uint8_t>(
                    (pg << 4) | (1u << 3) | ((attr >> 3) & 0x07));
                ink_argb   = palette_->ulap_colour(active_ula_palette_, ink_low6);
                paper_argb = palette_->ulap_colour(active_ula_palette_, paper_low6);
            } else {
                // G102 — std-ULA encoder per VHDL zxula.vhd:543-553.
                ink_argb   = lookup_colour(std_ula_ink_pixel(attr));
                paper_argb = lookup_colour(std_ula_paper_pixel(attr));
            }

            const bool pix_on = ((pixels >> src_bit) & 1) != 0;
            row[DISP_X + disp_x] = pix_on ? ink_argb : paper_argb;
        }
    }

    // Fill right border pixels (FB_WIDTH - DISP_X - DISP_W = 64 pixels under G104).
    uint32_t* right = row + DISP_X + DISP_W;
    for (int x = 0; x < FB_WIDTH - DISP_X - DISP_W; ++x) {
        right[x] = border_argb;
        // VHDL zxula.vhd:415 — border_active asserted on the right strip too.
        if (border_dst) border_dst[DISP_X + DISP_W + x] = true;
    }
}

// ---------------------------------------------------------------------------
// render_display_line_hicolour  (HI_COLOUR mode)
// ---------------------------------------------------------------------------
//
// Timex hi-colour mode: 256×192 with per-cell (8×1) colour attributes.
//
// Pixel data:    read from primary screen 0x4000 using the standard ZX
//               interleaved addressing.
// Attribute data: each attribute byte controls one 8-pixel column within a
//               single display row (not an 8-row character cell as in standard
//               mode).  The layout of the attribute area at 0x6000 mirrors the
//               standard pixel layout:
//
//                 attr_addr = 0x6000 | pixel_addr_offset(screen_row, col)
//
//               This matches the Timex hi-colour specification: the 6 KiB
//               attribute area starting at 0x6000 uses the same interleaved
//               row addressing as the pixel area at 0x4000, giving one
//               independent attribute byte per 8-pixel column per scanline.
//
// Flash and bright are derived from the attribute byte exactly as in standard
// mode.

void Ula::render_display_line_hicolour(uint32_t* row, int screen_row, Mmu& mmu,
                                       bool* border_dst)
{
    const uint16_t poff = pixel_addr_offset(screen_row, 0);
    // Wave D (S5.04) — VHDL zxula.vhd:218/235 shows the pixel vram_a uses
    // `screen_mode(0) & addr_p_spc_12_5 & px_1(7:3)`: in hi-colour mode the
    // pixel base follows the alt-file bit (0x4000 when alt=0, 0x6000 when
    // alt=1).  The attribute base stays at bank 1 (0x6000) because the
    // attribute fetch uses `'1' & addr_p_spc_12_5 & px_1(7:3)` when
    // screen_mode(1)='1' (zxula.vhd:239/249).  In HI_COLOUR+alt the pixel
    // and attribute bytes collide at the same address — a VHDL-literal
    // consequence that Wave D preserves.
    const uint16_t pixel_base = static_cast<uint16_t>(
        (alt_file_ ? 0x6000u : 0x4000u) | poff);
    const uint16_t attr_base  = static_cast<uint16_t>(0x6000u | poff);

    // Fill left border.  See render_display_line for the std-ULA encoder
    // derivation — border_active_d=1 forces paper cycle through std_ula_paper_pixel.
    const uint8_t border_attr_hc  = static_cast<uint8_t>(
        ((border_colour_ & 0x07) << 3) | (border_colour_ & 0x07));
    const uint32_t border_argb = lookup_colour(std_ula_paper_pixel(border_attr_hc));
    for (int x = 0; x < DISP_X; ++x) {
        row[x] = border_argb;
        if (border_dst) border_dst[x] = true;
    }

    // G103 — HI_COLOUR mode also goes through the ULA+ encoder when
    // ulap_en_ is set; sm2=false here because HI_COLOUR is mode 010 and
    // the screen_mode(2) bit is zero (only HI_RES sets it).
    const bool sm2 = false;
    for (int col = 0; col < 32; ++col) {
        const uint8_t pixels = vram_read(static_cast<uint16_t>(pixel_base + col), mmu);
        const uint8_t attr_raw = vram_read(static_cast<uint16_t>(attr_base  + col), mmu);

        // zxula.vhd:470 — flash XOR gated off when i_ulanext_en='1' or
        // i_ulap_en='1' (Waves B + C combined).
        uint8_t attr = attr_raw;
        const bool flash = (attr_raw & 0x80) != 0;
        if (flash && flash_phase_ && !ulanext_en_ && !ulap_en_) {
            const uint8_t ink_bits   = attr_raw       & 0x07;
            const uint8_t paper_bits = (attr_raw >> 3) & 0x07;
            attr = static_cast<uint8_t>(
                (attr_raw & 0xC0) | (ink_bits << 3) | paper_bits);
        }

        uint32_t ink_argb;
        uint32_t paper_argb;
        if (ulanext_en_ && palette_) {
            // G102 — ULAnext runtime path; see render_display_line for
            // full VHDL citations.  LR-140 — select_bgnd routes to the
            // NR $4A fallback (zxnext.vhd:6987-6991).
            const auto ink_pix   = compute_ulanext_pixel(
                /*pixel_en*/true,  /*border*/false, attr);
            const auto paper_pix = compute_ulanext_pixel(
                /*pixel_en*/false, /*border*/false, attr);
            ink_argb   = ink_pix.select_bgnd
                             ? select_bgnd_argb_
                             : palette_->ula_colour(active_ula_palette_,
                                                    ink_pix.pixel);
            paper_argb = paper_pix.select_bgnd
                             ? select_bgnd_argb_
                             : palette_->ula_colour(active_ula_palette_,
                                                    paper_pix.pixel);
        } else if (ulap_en_ && palette_) {
            // G103 — ULA+ encoder, see render_display_line for layout.
            const uint8_t pg = static_cast<uint8_t>((attr >> 6) & 0x03);
            const uint8_t ink_low6 = static_cast<uint8_t>(
                (pg << 4) | ((sm2 ? 1u : 0u) << 3) | (attr & 0x07));
            const uint8_t paper_low6 = static_cast<uint8_t>(
                (pg << 4) | (1u << 3) | ((attr >> 3) & 0x07));
            ink_argb   = palette_->ulap_colour(active_ula_palette_, ink_low6);
            paper_argb = palette_->ulap_colour(active_ula_palette_, paper_low6);
        } else {
            // G102 — std-ULA encoder per VHDL zxula.vhd:543-553.
            ink_argb   = lookup_colour(std_ula_ink_pixel(attr));
            paper_argb = lookup_colour(std_ula_paper_pixel(attr));
        }

        // VHDL zxula.vhd:390-393 — lo-res shift_reg_32 doubles each
        // shift_pbyte bit; HI_COLOUR is mode 010 (shift_screen_mode(2)='0')
        // so the same intrinsic doubling applies.  Each source bit emits
        // 2 adjacent framebuffer cells; per source byte = 16 cells emitted.
        uint32_t* dst = row + DISP_X + col * 16;
        for (int bit = 7; bit >= 0; --bit) {
            const uint32_t px = (pixels >> bit) & 1 ? ink_argb : paper_argb;
            *dst++ = px;
            *dst++ = px;
        }
    }

    // Fill right border (FB_WIDTH - DISP_X - DISP_W = 64 cells under G104).
    uint32_t* right = row + DISP_X + DISP_W;
    for (int x = 0; x < FB_WIDTH - DISP_X - DISP_W; ++x) {
        right[x] = border_argb;
        if (border_dst) border_dst[DISP_X + DISP_W + x] = true;
    }
}

// ---------------------------------------------------------------------------
// render_display_line_hires  (HI_RES mode)
// ---------------------------------------------------------------------------
//
// Timex hi-res mode provides 512 monochrome pixels per scanline by
// byte-interleaving two 256-pixel-wide screens.  G104 — native 512-px
// rendering, no approximation.
//
// VHDL zxula.vhd:131-138, 271-306, 384-406 (authoritative spec):
//
//   shift_pbyte <= (pbyte00 & pbyte01)         -- 16 bits = bytes from screen 0
//   shift_abyte <= (abyte00 & abyte01)         -- 16 bits = bytes from screen 1
//   shift_reg_32 <= shift_pbyte(15:8) & shift_abyte(15:8)
//                 & shift_pbyte(7:0)  & shift_abyte(7:0)
//                  when shift_screen_mode(2) = '1'
//   shift_reg_ld <= shift_left(shift_reg_32, scroll)   -- emits MSB-first
//
// So per source-column pair (cols N and N+1) the 32 emitted hi-res pixels are:
//   8 px from screen-0 col N (pbyte bits 7..0)
//   8 px from screen-1 col N (abyte bits 7..0)
//   8 px from screen-0 col N+1
//   8 px from screen-1 col N+1
//
// At column granularity that decomposes as "for each col 0..31: emit 8 px from
// s0[col] then 8 px from s1[col]", which is what we do.  Each column contributes
// 16 hi-res output pixels to the framebuffer at offset DISP_X + col * 16.
//
// Ink / paper derivation (G179, Issue #2 — VHDL zxula.vhd:419, 426-427):
//   border_clr_tmx <= "01" & (not i_port_ff_reg(5 downto 3)) & i_port_ff_reg(5 downto 3);
//   ...
//   if shift_screen_mode(2) = '1' then
//      attr_reg <= border_clr_tmx & border_clr_tmx;
// In HI_RES the WHOLE attr_reg (display + border) is loaded with this 8-bit
// `border_clr_tmx`.  Decoding the bit fields as a std-ULA attr:
//   bit 7 = '0', bit 6 = '1' (BRIGHT on)
//   bits 5:3 (PAPER nibble) = ~port_ff(5:3)
//   bits 2:0 (INK   nibble) =  port_ff(5:3)
// So the user-controllable "screen colour" is port_ff(5:3) and is rendered
// as bright ink + bright complementary paper.  BRIGHT is implicit (not
// user-controllable).  The previous derivation read port_ff(2:0) as ink and
// port_ff(5:3) as paper independently — that contradicts VHDL.

void Ula::render_display_line_hires(uint32_t* row, int screen_row, Mmu& mmu,
                                    bool* border_dst)
{
    // VHDL zxula.vhd:419 + 426-427: in HI_RES mode (shift_screen_mode(2)='1')
    // the whole attr_reg is loaded with border_clr_tmx, where
    //   border_clr_tmx <= "01" & (not port_ff(5:3)) & port_ff(5:3).
    // Synthesize the same 8-bit attr value: bit 6 (BRIGHT)=1, bits 5:3
    // (PAPER)=~port_ff(5:3), bits 2:0 (INK)=port_ff(5:3).
    const uint8_t paper_color = static_cast<uint8_t>((screen_mode_reg_ >> 3) & 0x07);
    const uint8_t hires_attr  = static_cast<uint8_t>(
          0x40                                  // bit 6 = BRIGHT = 1
        | ((~paper_color & 0x07) << 3)          // PAPER bits = ~port_ff(5:3)
        |  (paper_color & 0x07));               // INK   bits =  port_ff(5:3)

    // G167 — encoder dispatch: ULAnext (zxula.vhd:492-528), ULA+
    // (:531-541), std-ULA (:543-553).  All three encoders run in HI_RES
    // (sm2 = shift_screen_mode(2) = 1) against the same synthesized
    // `hires_attr` (= border_clr_tmx).  Mirror of the dispatch already
    // applied in render_display_line / render_display_line_hicolour /
    // render_border_line.  Strip-border (left/right of a display row) is
    // the encoder's border path (border_active_d=1, pixel_en=0).  Pre-
    // G167 the HI_RES display path bypassed the dispatch and always used
    // the std-ULA encoder — wrong under ULAnext/ULA+ palette modes.
    //
    // Note: hires_attr has bit 7 = 0 by construction, so the
    // flash XOR (zxula.vhd:470) cannot fire; no flash gate needed.
    uint32_t ink_argb;
    uint32_t paper_argb;
    uint32_t border_argb;
    if (ulanext_en_ && palette_) {
        // VHDL zxula.vhd:492-528 — ULAnext encoder, sm2 not consulted.
        const auto ink_pix    = compute_ulanext_pixel(
            /*pixel_en*/true,  /*border*/false, hires_attr);
        const auto paper_pix  = compute_ulanext_pixel(
            /*pixel_en*/false, /*border*/false, hires_attr);
        const auto border_pix = compute_ulanext_pixel(
            /*pixel_en*/false, /*border*/true,  hires_attr);
        // LR-140 — select_bgnd routes to the NR $4A fallback
        // (zxnext.vhd:6987-6991; border asserts it for format 0xFF,
        // zxula.vhd:498-502).
        ink_argb    = ink_pix.select_bgnd
                          ? select_bgnd_argb_
                          : palette_->ula_colour(active_ula_palette_, ink_pix.pixel);
        paper_argb  = paper_pix.select_bgnd
                          ? select_bgnd_argb_
                          : palette_->ula_colour(active_ula_palette_, paper_pix.pixel);
        border_argb = border_pix.select_bgnd
                          ? select_bgnd_argb_
                          : palette_->ula_colour(active_ula_palette_, border_pix.pixel);
    } else if (ulap_en_ && palette_) {
        // VHDL zxula.vhd:535-540 — ULA+ encoder; in HI_RES sm2=1, so
        // ula_pixel(3) = (sm2 OR not pixel_en) = 1 for both ink and paper
        // and border (border_active_d=1 forces pixel_en=0).  Top 2 bits
        // are dropped by ulap_colour() (low6 indexing per G103).  pg =
        // attr(7:6) = "01" by construction (hires_attr bit 6 = 1).
        const uint8_t pg = static_cast<uint8_t>((hires_attr >> 6) & 0x03);
        const uint8_t ink_low6   = static_cast<uint8_t>(
            (pg << 4) | (1u << 3) | (hires_attr & 0x07));
        const uint8_t paper_low6 = static_cast<uint8_t>(
            (pg << 4) | (1u << 3) | ((hires_attr >> 3) & 0x07));
        // Border: pixel_en=0 → ula_pixel(2:0) = attr(5:3), same as paper.
        const uint8_t border_low6 = paper_low6;
        ink_argb    = palette_->ulap_colour(active_ula_palette_, ink_low6);
        paper_argb  = palette_->ulap_colour(active_ula_palette_, paper_low6);
        border_argb = palette_->ulap_colour(active_ula_palette_, border_low6);
    } else {
        // VHDL zxula.vhd:543-553 — std-ULA encoder.  In std-ULA the
        // border path (pixel_en=0) and the display paper cycle yield the
        // same ula_pixel = 0x10 | (attr(6)<<3) | attr(5:3); border_argb
        // therefore equals paper_argb.  Pre-G167 baseline preserved:
        // for paper_color=N, ink = bright N (ula_pixel 0x08|N), paper =
        // bright(~N&7) (ula_pixel 0x18|(~N&7)).
        ink_argb    = lookup_colour(std_ula_ink_pixel(hires_attr));
        paper_argb  = lookup_colour(std_ula_paper_pixel(hires_attr));
        border_argb = paper_argb;
    }

    // The interleaved pixel row offset is the same for both screens.
    const uint16_t poff         = pixel_addr_offset(screen_row, 0);
    const uint16_t screen0_base = static_cast<uint16_t>(0x4000u | poff);
    const uint16_t screen1_base = static_cast<uint16_t>(0x6000u | poff);

    // Fill left border (DISP_X = 64 cells under G104).
    for (int x = 0; x < DISP_X; ++x) {
        row[x] = border_argb;
        if (border_dst) border_dst[x] = true;
    }

    // VHDL-faithful native 512 px: per column, emit 8 screen-0 pixels first
    // (MSB..LSB) then 8 screen-1 pixels (MSB..LSB).  Each column contributes
    // 16 framebuffer cells at base DISP_X + col * 16; total 512 cells across
    // 32 columns fills the entire display width.
    for (int col = 0; col < 32; ++col) {
        const uint8_t b0 = vram_read(static_cast<uint16_t>(screen0_base + col), mmu);
        const uint8_t b1 = vram_read(static_cast<uint16_t>(screen1_base + col), mmu);

        uint32_t* dst = row + DISP_X + col * 16;
        // Screen-0 byte first (VHDL shift_pbyte(15:8) lands at shift_reg_32(31:24)).
        for (int bit = 7; bit >= 0; --bit)
            *dst++ = (b0 >> bit) & 1 ? ink_argb : paper_argb;
        // Then screen-1 byte (VHDL shift_abyte(15:8) lands at shift_reg_32(23:16)).
        for (int bit = 7; bit >= 0; --bit)
            *dst++ = (b1 >> bit) & 1 ? ink_argb : paper_argb;
    }

    // Fill right border (FB_WIDTH - DISP_X - DISP_W = 64 cells under G104).
    uint32_t* right = row + DISP_X + DISP_W;
    for (int x = 0; x < FB_WIDTH - DISP_X - DISP_W; ++x) {
        right[x] = border_argb;
        if (border_dst) border_dst[DISP_X + DISP_W + x] = true;
    }
}

// ---------------------------------------------------------------------------
// render_border_line
// ---------------------------------------------------------------------------

void Ula::render_border_line(uint32_t* row, bool* border_dst)
{
    // Border routing per VHDL zxula.vhd:419, :443-448:
    //   - In HI_RES (or border_clr_tmx_src_ explicit), attr_reg is loaded
    //     with `border_clr_tmx <= "01" & (not port_ff(5:3)) & port_ff(5:3)`
    //     — an 8-bit attr value with bits 7:6 = "01", bits 5:3 = ~paper,
    //     bits 2:0 = paper.
    //   - Otherwise attr_reg holds `border_clr <= "00" & port_fe & port_fe`.
    // The 8-bit attr value then flows through the standard / ULAnext /
    // ULA+ encoder (line 543-554 / :492-528 / :531-541) with
    // `border_active_d=1` forcing pixel_en=0.  The resulting `ula_pixel`
    // indexes the single 256-entry × 2-bank ULA palette (zxnext.vhd:6981).
    //
    // G102 — every encoder path now feeds the SAME 256-entry palette,
    // including the std-ULA HI_RES TMX border (previously an approximation).
    const bool     use_tmx = border_clr_tmx_src_
                             || (mode_ == TimexScreenMode::HI_RES);
    if (!use_tmx) {
        // Standard non-Timex border path (port 0xFE bits 2:0).
        // VHDL :418 — border_clr <= "00" & port_fe(2:0) & port_fe(2:0)
        // (attr(5:3) = attr(2:0) = border_colour, attr(6)=0).  Through
        // std-ULA encoder paper cycle: ula_pixel = 0x10 | (border & 7).
        const uint8_t border_attr = static_cast<uint8_t>(
            ((border_colour_ & 0x07) << 3) | (border_colour_ & 0x07));
        const uint32_t border_argb = lookup_colour(std_ula_paper_pixel(border_attr));
        for (int x = 0; x < FB_WIDTH; ++x) {
            row[x] = border_argb;
            // VHDL zxula.vhd:415 — border_active_v is forced high on every
            // top/bottom border row (vc(8) OR (vc(7) AND vc(6))), so
            // every horizontal cell asserts ula_border_2.
            if (border_dst) border_dst[x] = true;
        }
        return;
    }

    // HI_RES / TMX border — VHDL-faithful 6-bit-via-8-bit encoding.
    const uint8_t paper_bits = static_cast<uint8_t>((screen_mode_reg_ >> 3) & 0x07);
    // border_clr_tmx_8bit per zxula.vhd:419.  bits 7:6 = "01", 5:3 = ~paper,
    // 2:0 = paper.
    const uint8_t btmx = static_cast<uint8_t>(
        0x40 | (((~paper_bits) & 0x07) << 3) | paper_bits);

    uint32_t border_argb;
    if (ulanext_en_ && palette_) {
        // VHDL zxula.vhd:504: ula_pixel = "10000" & attr(5:3) for border
        // (attr(5:3) = ~paper → idx = 0x80 | (~paper & 7)), and :498-502
        // additionally asserts ula_select_bgnd when format = 0xFF.
        // LR-140 — route through the encoder so the select_bgnd → NR $4A
        // substitution (zxnext.vhd:6987-6991) applies to this border too;
        // for format != 0xFF the encoded index is unchanged.
        const auto bp = compute_ulanext_pixel(/*pixel_en*/false,
                                              /*border*/true, btmx);
        border_argb = bp.select_bgnd
                          ? select_bgnd_argb_
                          : palette_->ula_colour(active_ula_palette_, bp.pixel);
    } else if (ulap_en_ && palette_) {
        // VHDL zxula.vhd:535-540 with border_active_d=1 (pixel_en=0):
        //   ula_pixel(7:3) = "11" & attr(7:6) & (sm2 OR not pixel_en)
        //                  = "11" & "01" & "1"  = 0x1B
        //   ula_pixel(2:0) = attr(5:3) = ~paper & 7
        // Hence ula_pixel = 0xD8 | (~paper & 7); the low 6 bits (the
        // 64-entry ULA+ slot index per G103's ulap_colour) are
        // 0x18 | (~paper & 7).
        const uint8_t low6 = static_cast<uint8_t>(0x18 | ((btmx >> 3) & 0x07));
        border_argb = palette_->ulap_colour(active_ula_palette_, low6);
    } else {
        // G102/G105 — std-ULA encoder for HI_RES TMX border.
        // VHDL :543-553 with the TMX 8-bit attr (bits 7:6="01",
        // 5:3=~paper, 2:0=paper) and border_active_d=1 (paper cycle):
        //   ula_pixel = 0x10 | (attr(6)<<3) | attr(5:3) = 0x10 | (~paper & 7)
        // (attr(7:6)="01" is silently dropped by the std-ULA encoder).
        // Indexes idx 0x10..0x17 in the 256-entry ULA palette — the
        // boot-default paper-cycle mirrors of the 16 ZX colours.
        border_argb = lookup_colour(std_ula_paper_pixel(btmx));
    }

    for (int x = 0; x < FB_WIDTH; ++x) {
        row[x] = border_argb;
        // HI_RES / TMX border path — every cell of a top/bottom border row
        // is border_active (zxula.vhd:415).
        if (border_dst) border_dst[x] = true;
    }
}

// ---------------------------------------------------------------------------
// Per-scanline NR 0x26 / NR 0x27 / NR 0x68 b2 scroll change log (G08)
// ---------------------------------------------------------------------------
//
// VHDL refs:
//   zxnext.vhd:5304 — NR 0x26 storage (nr_26_ula_scrollx <= nr_wr_dat).
//   zxnext.vhd:5307 — NR 0x27 storage (nr_27_ula_scrolly <= nr_wr_dat).
//   zxnext.vhd:5449 — NR 0x68 b2 storage (nr_68_ula_fine_scroll_x).
//   zxula.vhd:192-207 — py = i_vc + i_ula_scroll_y (cross-third wrap).
//   zxula.vhd:199    — px = fine & (hc(7:3)+scroll_x(7:3)) & scroll_x(2:0).
//   zxula.vhd:194-212 — px / py latch on falling i_CLK_7 when
//                      i_hc(3:0)='3' or 'B' (per character cell).
//
// Mid-frame Copper writes to these registers shift the scroll on the next
// character cell boundary. We approximate to per-scanline granularity (the
// log is keyed by scanline) which suffices for raster-bar and parallax
// effects whose splits are aligned to scanlines anyway. The implementation
// mirrors Layer2's per-scanline change log shape line-for-line.

void Ula::log_scroll_change()
{
    if (scroll_change_count_ >= MAX_SCROLL_CHANGES_PER_FRAME) {
        if (!scroll_overflow_warned_) {
            Log::ula()->warn(
                "Ula: scroll change-log full at line {} (cap {} per "
                "frame); further NR 0x26/0x27/0x68b2 writes this frame "
                "will not be per-scanline.",
                current_scroll_line_, MAX_SCROLL_CHANGES_PER_FRAME);
            scroll_overflow_warned_ = true;
        }
        return;
    }
    scroll_change_log_[scroll_change_count_++] = ScrollChange{
        current_scroll_line_,
        ula_scroll_x_coarse_,
        ula_scroll_y_,
        static_cast<uint8_t>(ula_fine_scroll_x_ ? 1 : 0),
    };
}

void Ula::start_frame_scroll()
{
    baseline_scroll_x_coarse_ = ula_scroll_x_coarse_;
    baseline_scroll_y_        = ula_scroll_y_;
    baseline_fine_scroll_x_   = static_cast<uint8_t>(ula_fine_scroll_x_ ? 1 : 0);
    scroll_change_count_      = 0;
    current_scroll_line_      = 0;
    scroll_render_cursor_     = 0;
    scroll_overflow_warned_   = false;
}

void Ula::rewind_scroll_to_baseline()
{
    ula_scroll_x_coarse_  = baseline_scroll_x_coarse_;
    ula_scroll_y_         = baseline_scroll_y_;
    ula_fine_scroll_x_    = baseline_fine_scroll_x_ != 0;
    scroll_render_cursor_ = 0;
}

void Ula::apply_scroll_changes_for_line(int line)
{
    const uint16_t lt = static_cast<uint16_t>(line);
    while (scroll_render_cursor_ < scroll_change_count_
        && scroll_change_log_[scroll_render_cursor_].line == lt) {
        const auto& c = scroll_change_log_[scroll_render_cursor_++];
        ula_scroll_x_coarse_ = c.scroll_x_coarse;
        ula_scroll_y_        = c.scroll_y;
        ula_fine_scroll_x_   = c.fine_scroll_x != 0;
    }
}

void Ula::flush_remaining_scroll_changes()
{
    // Drain ULA scroll entries (NR 0x26/0x27/0x68 b2) that landed in
    // vblank. See Renderer::render_frame for rationale.
    while (scroll_render_cursor_ < scroll_change_count_) {
        const auto& c = scroll_change_log_[scroll_render_cursor_++];
        ula_scroll_x_coarse_ = c.scroll_x_coarse;
        ula_scroll_y_        = c.scroll_y;
        ula_fine_scroll_x_   = c.fine_scroll_x != 0;
    }
}

void Ula::save_state(StateWriter& w) const
{
    w.write_bool(ula_enabled_);
    w.write_bool(vram_use_bank7_);
    w.write_u8(clip_x1_); w.write_u8(clip_x2_);
    w.write_u8(clip_y1_); w.write_u8(clip_y2_);
    w.write_u8(border_colour_);
    w.write_bytes(border_per_line_.data(), FB_HEIGHT);
    w.write_i32(flash_counter_);
    w.write_bool(flash_phase_);
    w.write_u8(screen_mode_reg_);
    w.write_u8(static_cast<uint8_t>(mode_));

    // Phase-1 scaffold state — appended so legacy snapshots still load the
    // prefix cleanly while new snapshots round-trip the full register file.
    w.write_u8(ula_scroll_x_coarse_);
    w.write_u8(ula_scroll_y_);
    w.write_bool(ula_fine_scroll_x_);
    w.write_u8(ulanext_format_);
    w.write_bool(ulanext_en_);
    w.write_bool(ulap_en_);
    w.write_bool(alt_file_);
    w.write_bool(shadow_screen_en_);
    w.write_bool(border_clr_tmx_src_);
    w.write_u8(ulap_mode_);

    // Per-scanline port-0xFF change-log (G07).  Layer 2 / palette only
    // serialise the live state because the log is regenerated each
    // frame, but S5-PSL.05 explicitly requires a save/load round-trip
    // that preserves the in-flight log so post-load re-render through
    // apply_changes_for_line() reproduces the pre-save frame.  Persist
    // the baseline + tagged entries; cursor + overflow flag are
    // transient render-time state and don't need to round-trip.
    //
    // Issue #42 — the log is written at its FULL CAPACITY, not at the
    // live count: RewindBuffer sizes every slot from a single dry-run
    // save_state() and then requires each snapshot to be exactly that
    // size, so a count-prefixed variable-length field silently broke
    // rewind the first time a guest touched port 0xFF (3 bytes over,
    // snapshot dropped).  Entries past `n` are stale, and load_state
    // ignores them; the constant width is what keeps the snapshot size
    // an invariant instead of a property of guest behaviour.
    w.write_u8(baseline_port_ff_);
    w.write_u16(current_line_);
    const uint16_t n = static_cast<uint16_t>(port_ff_count_);
    w.write_u16(n);
    for (size_t i = 0; i < MAX_CHANGES_PER_FRAME; ++i) {
        w.write_u16(port_ff_log_[i].line);
        w.write_u8(port_ff_log_[i].value);
    }
}

void Ula::load_state(StateReader& r)
{
    ula_enabled_ = r.read_bool();
    vram_use_bank7_ = r.read_bool();
    clip_x1_ = r.read_u8(); clip_x2_ = r.read_u8();
    clip_y1_ = r.read_u8(); clip_y2_ = r.read_u8();
    border_colour_ = r.read_u8();
    r.read_bytes(border_per_line_.data(), FB_HEIGHT);
    flash_counter_ = r.read_i32();
    flash_phase_ = r.read_bool();
    screen_mode_reg_ = r.read_u8();
    mode_ = static_cast<TimexScreenMode>(r.read_u8());

    ula_scroll_x_coarse_ = r.read_u8();
    ula_scroll_y_        = r.read_u8();
    ula_fine_scroll_x_   = r.read_bool();
    ulanext_format_      = r.read_u8();
    ulanext_en_          = r.read_bool();
    ulap_en_             = r.read_bool();
    alt_file_            = r.read_bool();
    shadow_screen_en_    = r.read_bool();
    border_clr_tmx_src_  = r.read_bool();
    ulap_mode_           = r.read_u8();

    // Per-scanline port-0xFF change-log (G07) — paired with save_state.
    baseline_port_ff_      = r.read_u8();
    current_line_          = r.read_u16();
    const uint16_t n       = r.read_u16();
    // A corrupt count cannot overrun the array: the stream always carries
    // exactly MAX_CHANGES_PER_FRAME entries (see save_state), so clamp the
    // live count and read the full fixed-width block regardless.
    port_ff_count_         = (n <= MAX_CHANGES_PER_FRAME) ? n : MAX_CHANGES_PER_FRAME;
    port_ff_render_cursor_ = 0;
    port_ff_overflow_warned_ = false;
    for (size_t i = 0; i < MAX_CHANGES_PER_FRAME; ++i) {
        port_ff_log_[i].line  = r.read_u16();
        port_ff_log_[i].value = r.read_u8();
    }
}

// ---------------------------------------------------------------------------
// Per-scanline active-palette select (G10) — VHDL refs in ula.h
// ---------------------------------------------------------------------------
//
// NR 0x43 b1-3 (active_ula / active_layer2 / active_sprite palette)
// VHDL zxnext.vhd:5391-5393 latches; mux at zxnext.vhd:6825-6828.
// NR 0x6B b4 (tm_palette_select)
// VHDL zxnext.vhd:5462 latch (control(4)); mux at zxnext.vhd:6826.
//
// The two latch groups are physically independent in the VHDL — a write
// to NR 0x43 cannot perturb tm_palette_select (and vice versa) — so we
// keep two separate change-logs.  S17.03 verifies that independence.

void Ula::log_palsel43_change()
{
    if (palsel43_change_count_ >= MAX_PALSEL_CHANGES_PER_FRAME) {
        if (!palsel43_overflow_warned_) {
            Log::video()->warn(
                "Ula: NR 0x43 selector change-log full at line {} (cap {} "
                "per frame); further NR 0x43 b1-3 writes this frame will "
                "not be per-scanline.",
                palsel_current_line_, MAX_PALSEL_CHANGES_PER_FRAME);
            palsel43_overflow_warned_ = true;
        }
        return;
    }
    palsel43_change_log_[palsel43_change_count_++] = PalSel43Change{
        palsel_current_line_,
        active_ula_palette_,
        active_l2_palette_,
        active_spr_palette_,
    };
}

void Ula::log_palsel6b_change()
{
    if (palsel6b_change_count_ >= MAX_PALSEL_CHANGES_PER_FRAME) {
        if (!palsel6b_overflow_warned_) {
            Log::video()->warn(
                "Ula: NR 0x6B b4 selector change-log full at line {} (cap "
                "{} per frame); further NR 0x6B b4 writes this frame will "
                "not be per-scanline.",
                palsel_current_line_, MAX_PALSEL_CHANGES_PER_FRAME);
            palsel6b_overflow_warned_ = true;
        }
        return;
    }
    palsel6b_change_log_[palsel6b_change_count_++] = PalSel6bChange{
        palsel_current_line_,
        active_tm_palette_,
    };
}

void Ula::palsel_start_frame()
{
    baseline_active_ula_pal_   = active_ula_palette_;
    baseline_active_l2_pal_    = active_l2_palette_;
    baseline_active_spr_pal_   = active_spr_palette_;
    palsel43_change_count_     = 0;
    palsel43_render_cursor_    = 0;
    palsel43_overflow_warned_  = false;

    baseline_active_tm_pal_    = active_tm_palette_;
    palsel6b_change_count_     = 0;
    palsel6b_render_cursor_    = 0;
    palsel6b_overflow_warned_  = false;

    palsel_current_line_       = 0;
}

void Ula::palsel_rewind_to_baseline()
{
    active_ula_palette_     = baseline_active_ula_pal_;
    active_l2_palette_      = baseline_active_l2_pal_;
    active_spr_palette_     = baseline_active_spr_pal_;
    palsel43_render_cursor_ = 0;

    active_tm_palette_      = baseline_active_tm_pal_;
    palsel6b_render_cursor_ = 0;
}

void Ula::palsel_apply_changes_for_line(int line)
{
    const uint16_t lt = static_cast<uint16_t>(line);

    while (palsel43_render_cursor_ < palsel43_change_count_
        && palsel43_change_log_[palsel43_render_cursor_].line == lt) {
        const auto& c = palsel43_change_log_[palsel43_render_cursor_++];
        active_ula_palette_ = c.active_ula;
        active_l2_palette_  = c.active_l2;
        active_spr_palette_ = c.active_spr;
    }

    while (palsel6b_render_cursor_ < palsel6b_change_count_
        && palsel6b_change_log_[palsel6b_render_cursor_].line == lt) {
        const auto& c = palsel6b_change_log_[palsel6b_render_cursor_++];
        active_tm_palette_ = c.active_tm;
    }
}

void Ula::palsel_flush_remaining_changes()
{
    // Drain palette-select entries (NR 0x43 + NR 0x6B b4) that landed in
    // vblank. See Renderer::render_frame for rationale.
    while (palsel43_render_cursor_ < palsel43_change_count_) {
        const auto& c = palsel43_change_log_[palsel43_render_cursor_++];
        active_ula_palette_ = c.active_ula;
        active_l2_palette_  = c.active_l2;
        active_spr_palette_ = c.active_spr;
    }
    while (palsel6b_render_cursor_ < palsel6b_change_count_) {
        const auto& c = palsel6b_change_log_[palsel6b_render_cursor_++];
        active_tm_palette_ = c.active_tm;
    }
}
