#include "video/layer2.h"
#include "video/palette.h"
#include "memory/ram.h"
#include "core/log.h"
#include "core/saveable.h"

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void Layer2::reset()
{
    active_bank_    = 8;
    shadow_bank_    = 11;
    scroll_x_       = 0;
    scroll_y_       = 0;
    palette_offset_ = 0;
    resolution_     = 0;
    enabled_        = false;
    // VHDL zxnext.vhd:4959-4962 NR 0x18 (Layer 2 clip) reset defaults:
    //   clip_x1=0x00, clip_x2=0xFF, clip_y1=0x00, clip_y2=0xBF (191).
    clip_x1_        = 0x00;
    clip_x2_        = 0xFF;
    clip_y1_        = 0x00;
    clip_y2_        = 0xBF;

    // Per-scanline change log cleared. Baseline reset to current (zero)
    // state; start_frame() will re-snapshot from the live values at the
    // next frame boundary.
    change_count_      = 0;
    current_line_      = 0;
    render_cursor_     = 0;
    overflow_warned_   = false;
    baseline_scroll_x_ = 0;
    baseline_scroll_y_ = 0;

    clip_change_count_     = 0;
    clip_render_cursor_    = 0;
    clip_overflow_warned_  = false;
    baseline_clip_x1_      = 0x00;
    baseline_clip_x2_      = 0xFF;
    baseline_clip_y1_      = 0x00;
    baseline_clip_y2_      = 0xBF;

    bank_change_count_     = 0;
    bank_render_cursor_    = 0;
    bank_overflow_warned_  = false;
    baseline_active_bank_  = 8;
    baseline_shadow_bank_  = 11;

    enable_change_count_    = 0;
    enable_render_cursor_   = 0;
    enable_overflow_warned_ = false;
    baseline_enabled_       = false;

    nr70_change_count_       = 0;
    nr70_render_cursor_      = 0;
    nr70_overflow_warned_    = false;
    baseline_resolution_     = 0;
    baseline_palette_offset_ = 0;
}

// ---------------------------------------------------------------------------
// Per-scanline change log
// ---------------------------------------------------------------------------

void Layer2::log_scroll_change()
{
    if (change_count_ >= MAX_CHANGES_PER_FRAME) {
        if (!overflow_warned_) {
            Log::video()->warn(
                "Layer2: scroll change-log full at line {} (cap {} per "
                "frame); further NR 0x16/0x17/0x71 writes this frame "
                "will not be per-scanline.",
                current_line_, MAX_CHANGES_PER_FRAME);
            overflow_warned_ = true;
        }
        return;
    }
    change_log_[change_count_++] = ScrollChange{
        current_line_,
        scroll_x_,
        scroll_y_,
    };
}

void Layer2::log_clip_change()
{
    if (clip_change_count_ >= MAX_CHANGES_PER_FRAME) {
        if (!clip_overflow_warned_) {
            Log::video()->warn(
                "Layer2: clip change-log full at line {} (cap {} per "
                "frame); further NR 0x18 writes this frame will not be "
                "per-scanline.",
                current_line_, MAX_CHANGES_PER_FRAME);
            clip_overflow_warned_ = true;
        }
        return;
    }
    clip_change_log_[clip_change_count_++] = ClipChange{
        current_line_, clip_x1_, clip_x2_, clip_y1_, clip_y2_,
    };
}

void Layer2::log_bank_change()
{
    if (bank_change_count_ >= MAX_CHANGES_PER_FRAME) {
        if (!bank_overflow_warned_) {
            Log::video()->warn(
                "Layer2: bank change-log full at line {} (cap {} per "
                "frame); further NR 0x12/0x13 writes this frame will "
                "not be per-scanline.",
                current_line_, MAX_CHANGES_PER_FRAME);
            bank_overflow_warned_ = true;
        }
        return;
    }
    bank_change_log_[bank_change_count_++] = BankChange{
        current_line_, active_bank_, shadow_bank_,
    };
}

void Layer2::log_enable_change()
{
    if (enable_change_count_ >= MAX_CHANGES_PER_FRAME) {
        if (!enable_overflow_warned_) {
            Log::video()->warn(
                "Layer2: enable change-log full at line {} (cap {} per "
                "frame); further set_enabled writes this frame will "
                "not be per-scanline.",
                current_line_, MAX_CHANGES_PER_FRAME);
            enable_overflow_warned_ = true;
        }
        return;
    }
    enable_change_log_[enable_change_count_++] = EnableChange{
        current_line_, enabled_,
    };
}

void Layer2::log_nr70_change()
{
    if (nr70_change_count_ >= MAX_CHANGES_PER_FRAME) {
        if (!nr70_overflow_warned_) {
            Log::video()->warn(
                "Layer2: NR 0x70 change-log full at line {} (cap {} per "
                "frame); further NR 0x70 writes this frame will not be "
                "per-scanline.",
                current_line_, MAX_CHANGES_PER_FRAME);
            nr70_overflow_warned_ = true;
        }
        return;
    }
    nr70_change_log_[nr70_change_count_++] = Nr70Change{
        current_line_, resolution_, palette_offset_,
    };
}

void Layer2::start_frame()
{
    baseline_scroll_x_ = scroll_x_;
    baseline_scroll_y_ = scroll_y_;
    change_count_      = 0;
    render_cursor_     = 0;
    current_line_      = 0;
    overflow_warned_   = false;

    baseline_clip_x1_      = clip_x1_;
    baseline_clip_x2_      = clip_x2_;
    baseline_clip_y1_      = clip_y1_;
    baseline_clip_y2_      = clip_y2_;
    clip_change_count_     = 0;
    clip_render_cursor_    = 0;
    clip_overflow_warned_  = false;

    baseline_active_bank_  = active_bank_;
    baseline_shadow_bank_  = shadow_bank_;
    bank_change_count_     = 0;
    bank_render_cursor_    = 0;
    bank_overflow_warned_  = false;

    baseline_enabled_       = enabled_;
    enable_change_count_    = 0;
    enable_render_cursor_   = 0;
    enable_overflow_warned_ = false;

    baseline_resolution_     = resolution_;
    baseline_palette_offset_ = palette_offset_;
    nr70_change_count_       = 0;
    nr70_render_cursor_      = 0;
    nr70_overflow_warned_    = false;
}

void Layer2::rewind_to_baseline()
{
    scroll_x_              = baseline_scroll_x_;
    scroll_y_              = baseline_scroll_y_;
    render_cursor_         = 0;

    clip_x1_               = baseline_clip_x1_;
    clip_x2_               = baseline_clip_x2_;
    clip_y1_               = baseline_clip_y1_;
    clip_y2_               = baseline_clip_y2_;
    clip_render_cursor_    = 0;

    active_bank_           = baseline_active_bank_;
    shadow_bank_           = baseline_shadow_bank_;
    bank_render_cursor_    = 0;

    enabled_               = baseline_enabled_;
    enable_render_cursor_  = 0;

    resolution_            = baseline_resolution_;
    palette_offset_        = baseline_palette_offset_;
    nr70_render_cursor_    = 0;
}

void Layer2::apply_changes_for_line(int line)
{
    const uint16_t lt = static_cast<uint16_t>(line);

    while (render_cursor_ < change_count_
        && change_log_[render_cursor_].line == lt) {
        const auto& c = change_log_[render_cursor_++];
        scroll_x_ = c.scroll_x;
        scroll_y_ = c.scroll_y;
    }

    while (clip_render_cursor_ < clip_change_count_
        && clip_change_log_[clip_render_cursor_].line == lt) {
        const auto& c = clip_change_log_[clip_render_cursor_++];
        clip_x1_ = c.x1;
        clip_x2_ = c.x2;
        clip_y1_ = c.y1;
        clip_y2_ = c.y2;
    }

    while (bank_render_cursor_ < bank_change_count_
        && bank_change_log_[bank_render_cursor_].line == lt) {
        const auto& c = bank_change_log_[bank_render_cursor_++];
        active_bank_ = c.active_bank;
        shadow_bank_ = c.shadow_bank;
    }

    while (enable_render_cursor_ < enable_change_count_
        && enable_change_log_[enable_render_cursor_].line == lt) {
        const auto& c = enable_change_log_[enable_render_cursor_++];
        enabled_ = c.enabled;
    }

    while (nr70_render_cursor_ < nr70_change_count_
        && nr70_change_log_[nr70_render_cursor_].line == lt) {
        const auto& c = nr70_change_log_[nr70_render_cursor_++];
        resolution_     = c.resolution;
        palette_offset_ = c.palette_offset;
    }
}

void Layer2::flush_remaining_changes()
{
    // Drain entries the per-line render loop did not reach (line >=
    // FB_HEIGHT, vblank). See Renderer::render_frame for the rationale —
    // this is the same pattern as PaletteManager::flush_remaining_changes
    // and Tilemap::flush_remaining_nr6b_changes.
    while (render_cursor_ < change_count_) {
        const auto& c = change_log_[render_cursor_++];
        scroll_x_ = c.scroll_x;
        scroll_y_ = c.scroll_y;
    }
    while (clip_render_cursor_ < clip_change_count_) {
        const auto& c = clip_change_log_[clip_render_cursor_++];
        clip_x1_ = c.x1;
        clip_x2_ = c.x2;
        clip_y1_ = c.y1;
        clip_y2_ = c.y2;
    }
    while (bank_render_cursor_ < bank_change_count_) {
        const auto& c = bank_change_log_[bank_render_cursor_++];
        active_bank_ = c.active_bank;
        shadow_bank_ = c.shadow_bank;
    }
    while (enable_render_cursor_ < enable_change_count_) {
        const auto& c = enable_change_log_[enable_render_cursor_++];
        enabled_ = c.enabled;
    }
    while (nr70_render_cursor_ < nr70_change_count_) {
        const auto& c = nr70_change_log_[nr70_render_cursor_++];
        resolution_     = c.resolution;
        palette_offset_ = c.palette_offset;
    }
}

// ---------------------------------------------------------------------------
// NextREG 0x70 — Layer 2 control
// ---------------------------------------------------------------------------

void Layer2::set_control(uint8_t val)
{
    resolution_     = (val >> 4) & 0x03;
    palette_offset_ = val & 0x0F;
    log_nr70_change();
}

// ---------------------------------------------------------------------------
// Render one scanline
// ---------------------------------------------------------------------------
//
// VHDL reference: layer2.vhd
//
// Memory address generation:
//   256x192:  addr = y[7:0] & x[7:0]         (row-major)
//   320x256:  addr = x[8:0] & y[7:0]         (column-major, 8bpp)
//   640x256:  addr = x[8:0] & y[7:0]         (column-major, 4bpp, 2px/byte)
//
// Bank mapping:
//   layer2_bank_eff = ((0 & bank[6:4]) + 1) & bank[3:0]
//   layer2_addr_eff = (bank_eff + addr[16:14]) & addr[13:0]
//   Physical RAM offset = layer2_addr_eff * 2 bytes (SRAM words)
//   But in our emulator, RAM is byte-addressed from bank base.

static inline uint32_t compute_ram_addr(uint8_t active_bank, uint32_t l2_addr,
                                        bool rom_in_sram)
{
    // VHDL layer2.vhd:172 applies a +1 bank transform to skip ROM banks
    // in the shared physical SRAM. In the emulator, RAM and ROM are separate
    // objects, and the MMU also uses raw page numbers without the +1. Both
    // paths must agree, so we use the raw bank number here too.
    //
    // Next mode (rom_in_sram=true): the effective SRAM page per VHDL
    // zxnext.vhd:2964 mmu_A21_A13 formula adds +0x20 in 8K-page units,
    // i.e. +16 in 16K-bank units. The Mmu does the same in to_sram_page
    // for RAM slot reads, so firmware MMU writes to bank N land at the
    // same SRAM location that Layer 2 fetches here (N+16).
    int bank_16k = active_bank;
    int sub_bank = static_cast<int>(l2_addr >> 14);      // which 16K chunk (0-4)
    int offset   = static_cast<int>(l2_addr & 0x3FFF);   // offset within 16K
    int final_bank = bank_16k + sub_bank;
    if (rom_in_sram && final_bank < 16) {
        // Bank 5 / bank 7-lower dual-port VRAM exception (Mmu::to_sram_page).
        // Layer 2 wouldn't normally use bank 5 or bank 7 as its data banks,
        // but guard anyway so the shift matches Mmu's rule byte-for-byte.
        bool is_bank5_page = (final_bank == 5);                 // pages 0x0A, 0x0B
        bool is_bank7_lower = (final_bank * 2) == 0x0E;         // page 0x0E (bank 7 low)
        if (!is_bank5_page && !is_bank7_lower) final_bank += 16;
    }
    return static_cast<uint32_t>(final_bank * 16384 + offset);
}

void Layer2::render_scanline_debug(uint32_t* dst, int row, const Ram& ram,
                                   const PaletteManager& palette, uint8_t bank,
                                   bool rom_in_sram)
{
    const bool saved_enabled = enabled_;
    const uint8_t saved_bank = active_bank_;
    enabled_      = true;
    active_bank_  = bank;
    // Debugger view doesn't need per-pixel priority info — pass nullptr.
    render_scanline(dst, row, ram, palette, rom_in_sram, /*priority_dst=*/nullptr);
    enabled_      = saved_enabled;
    active_bank_  = saved_bank;
}

void Layer2::render_scanline(uint32_t* dst, int row, const Ram& ram,
                             const PaletteManager& palette,
                             bool rom_in_sram,
                             bool* priority_dst) const
{
    if (!enabled_)
        return;

    // VHDL transparency: compares the 8-bit RRRGGGBB palette output
    // (NOT the raw pixel index) against the global transparency colour
    // (NextREG 0x14).  See zxnext.vhd line 7121.
    uint8_t transp_rgb = palette.global_transparency();

    if (resolution_ == 0) {
        // ---------------------------------------------------------------
        // 256x192 @ 8bpp (row-major)
        // ---------------------------------------------------------------
        // row is a framebuffer row (0-255). Display area is rows 32-223.
        // G104 Phase 3: emit at canonical 640-grid. Each source pixel
        // (256 across) is pixel-doubled into two adjacent destination
        // cells, spanning the display strip [DISP_X..DISP_X+512). VHDL
        // layer2.vhd at narrow res samples at 7 MHz and the compositor
        // re-samples the layer at 14 MHz; the doubling here matches that
        // 1-pixel-into-2-clocks expansion.
        static constexpr int DISP_Y = 32;
        static constexpr int DISP_X = 64;
        int y = row - DISP_Y;
        if (y < 0 || y >= 192)
            return;

        // Clip Y check on DESTINATION row (pre-scroll), per VHDL
        // layer2.vhd:167 — clip uses `vc_eff`, not `y_pre`.
        if (y < clip_y1_ || y > clip_y2_)
            return;

        // Y scroll wraps at 192.
        int src_y = (y + scroll_y_) % 192;

        // Bank and row offset.
        int third = src_y / 64;
        int row_in_third = src_y % 64;
        uint32_t l2_addr_base = static_cast<uint32_t>(src_y) * 256;

        for (int x = 0; x < 256; ++x) {
            // Clip X check on DESTINATION column (pre-scroll), per VHDL
            // layer2.vhd:167 — clip uses `hc_eff`, not `x_pre`. Without
            // this, the 8-pixel "clipped band" wanders through the
            // display as L2 scrolls (parallax.nex bottom band exposed
            // it: side gutters that should be black showed graphics).
            // VHDL clip space at narrow res is 9-bit but the high bit is
            // 0 (clip_x1_q = '0' & i_clip_x1, layer2.vhd:130) so direct
            // 8-bit comparison against x (0..255) is faithful.
            if (x < clip_x1_ || x > clip_x2_)
                continue;

            int src_x = (x + (scroll_x_ & 0xFF)) & 0xFF;

            uint32_t l2_addr = l2_addr_base + src_x;
            uint32_t ram_addr = compute_ram_addr(active_bank_, l2_addr, rom_in_sram);
            uint8_t pixel = ram.read(ram_addr);

            uint8_t colour_idx = static_cast<uint8_t>(
                ((pixel >> 4) + palette_offset_) << 4 | (pixel & 0x0F));

            // Transparency: compare palette RRRGGGBB against global transparent colour.
            if (palette.layer2_rgb8(colour_idx) == transp_rgb)
                continue;

            // Pixel-double: each 256-mode source pixel writes two
            // 640-grid cells (matches VHDL's narrow-res 7 MHz output
            // re-sampled at the compositor's 14 MHz pixel clock).
            uint32_t argb = palette.layer2_colour(colour_idx);
            dst[DISP_X + 2 * x]     = argb;
            dst[DISP_X + 2 * x + 1] = argb;
            // VHDL zxnext.vhd:7050 — palette bit 15 (NR 0x44 b7) drives
            // layer2_priority_2 per-pixel for opaque L2 pixels. Compositor
            // uses this to promote L2 above sprites (zxnext.vhd:7220).
            if (priority_dst) {
                const bool prio = palette.layer2_priority_high(colour_idx);
                priority_dst[DISP_X + 2 * x]     = prio;
                priority_dst[DISP_X + 2 * x + 1] = prio;
            }
        }
    }
    else if (resolution_ == 1) {
        // ---------------------------------------------------------------
        // 320x256 @ 8bpp (column-major: addr = x * 256 + y)
        // ---------------------------------------------------------------
        // G104 Phase 3: 320 source pixels → 640 emitted (each doubled).
        // The 320-mode source covers the entire 640-wide framebuffer
        // (no border on the sides) — VHDL layer2.vhd:164 marks the
        // wide mode active over hc_eff = 0..319.
        if (row < 0 || row >= 256)
            return;

        // Clip Y on DESTINATION row (pre-scroll), per VHDL layer2.vhd:167.
        if (row < clip_y1_ || row > clip_y2_)
            return;

        // Y scroll wraps at 256 (natural 8-bit wrap).
        uint8_t src_y = static_cast<uint8_t>(row + scroll_y_);

        // VHDL clip for wide mode: clip_x1_q = i_clip_x1 & '0',
        //                          clip_x2_q = i_clip_x2 & '1'
        // (layer2.vhd:133-134). 9-bit register space spans 0..511; the
        // 8-bit clip register is therefore in units of "2 source pixels"
        // and `<<1` maps it onto the source column index x ∈ 0..319.
        // hc_eff (the per-pixel comparison signal at line 167) traverses
        // the same 0..319 source space in 320-mode, so direct comparison
        // against x is VHDL-faithful.
        uint16_t clip_x1_eff = static_cast<uint16_t>(clip_x1_) << 1;
        uint16_t clip_x2_eff = (static_cast<uint16_t>(clip_x2_) << 1) | 1;

        for (int x = 0; x < 320; ++x) {
            // Clip X on DESTINATION column (pre-scroll), per VHDL
            // layer2.vhd:167.
            if (x < clip_x1_eff || x > clip_x2_eff)
                continue;

            // X scroll with wrap at 320.
            int src_x_pre = x + (scroll_x_ & 0x1FF);
            int src_x = (src_x_pre >= 320) ? (src_x_pre - 320) : src_x_pre;

            // Column-major: addr = x * 256 + y (17-bit).
            uint32_t l2_addr = static_cast<uint32_t>(src_x) * 256 + src_y;
            uint32_t ram_addr = compute_ram_addr(active_bank_, l2_addr, rom_in_sram);
            uint8_t pixel = ram.read(ram_addr);

            uint8_t colour_idx = static_cast<uint8_t>(
                ((pixel >> 4) + palette_offset_) << 4 | (pixel & 0x0F));

            if (palette.layer2_rgb8(colour_idx) == transp_rgb)
                continue;

            // Pixel-double: 320 source → 640 framebuffer cells.
            uint32_t argb = palette.layer2_colour(colour_idx);
            dst[2 * x]     = argb;
            dst[2 * x + 1] = argb;
            // VHDL zxnext.vhd:7050 — see narrow-mode comment above.
            if (priority_dst) {
                const bool prio = palette.layer2_priority_high(colour_idx);
                priority_dst[2 * x]     = prio;
                priority_dst[2 * x + 1] = prio;
            }
        }
    }
    else {
        // ---------------------------------------------------------------
        // 640x256 @ 4bpp (column-major: addr = x * 256 + y, 2 px/byte)
        // ---------------------------------------------------------------
        // resolution_ == 2 or 3 both select this mode (VHDL: resolution(1)='1').
        // G104 Phase 3: native 640-emit only; the legacy 320 downsampled
        // fallback branch is gone.
        if (row < 0 || row >= 256)
            return;

        // Clip Y on DESTINATION row (pre-scroll), per VHDL layer2.vhd:167.
        if (row < clip_y1_ || row > clip_y2_)
            return;

        uint8_t src_y = static_cast<uint8_t>(row + scroll_y_);

        // Wide-mode clip (layer2.vhd:133-134): 9-bit, 0..511 grid.
        uint16_t clip_x1_eff = static_cast<uint16_t>(clip_x1_) << 1;
        uint16_t clip_x2_eff = (static_cast<uint16_t>(clip_x2_) << 1) | 1;

        // Each memory address holds 2 horizontal pixels (left = high
        // nibble, right = low nibble). Iterate 320 bytes; emit two
        // adjacent destination cells per byte for the canonical 640.
        for (int col = 0; col < 320; ++col) {
            // Clip X on DESTINATION column (pre-scroll), per VHDL
            // layer2.vhd:167. Compare column byte-index directly
            // against the doubled clip register (preserved from the
            // pre-G104 semantics); do NOT divide.
            if (col < clip_x1_eff || col > clip_x2_eff)
                continue;

            int src_col_pre = col + (scroll_x_ & 0x1FF);
            int src_col = (src_col_pre >= 320) ? (src_col_pre - 320) : src_col_pre;

            uint32_t l2_addr = static_cast<uint32_t>(src_col) * 256 + src_y;
            uint32_t ram_addr = compute_ram_addr(active_bank_, l2_addr, rom_in_sram);
            uint8_t byte = ram.read(ram_addr);

            // High nibble = left pixel.
            uint8_t left_nib = (byte >> 4) & 0x0F;
            uint8_t left_idx = static_cast<uint8_t>((palette_offset_ << 4) | left_nib);
            if (palette.layer2_rgb8(left_idx) != transp_rgb) {
                dst[col * 2] = palette.layer2_colour(left_idx);
                // VHDL zxnext.vhd:7050 — per-pixel L2 priority bit.
                if (priority_dst)
                    priority_dst[col * 2] = palette.layer2_priority_high(left_idx);
            }

            // Low nibble = right pixel.
            uint8_t right_nib = byte & 0x0F;
            uint8_t right_idx = static_cast<uint8_t>((palette_offset_ << 4) | right_nib);
            if (palette.layer2_rgb8(right_idx) != transp_rgb) {
                dst[col * 2 + 1] = palette.layer2_colour(right_idx);
                if (priority_dst)
                    priority_dst[col * 2 + 1] = palette.layer2_priority_high(right_idx);
            }
        }
    }
}

void Layer2::save_state(StateWriter& w) const
{
    w.write_u8(active_bank_);
    w.write_u8(shadow_bank_);
    w.write_u16(scroll_x_);
    w.write_u8(scroll_y_);
    w.write_u8(palette_offset_);
    w.write_u8(resolution_);
    w.write_bool(enabled_);
    w.write_u8(clip_x1_); w.write_u8(clip_x2_);
    w.write_u8(clip_y1_); w.write_u8(clip_y2_);
}

void Layer2::load_state(StateReader& r)
{
    active_bank_ = r.read_u8();
    shadow_bank_ = r.read_u8();
    scroll_x_ = r.read_u16();
    scroll_y_ = r.read_u8();
    palette_offset_ = r.read_u8();
    resolution_ = r.read_u8();
    enabled_ = r.read_bool();
    clip_x1_ = r.read_u8(); clip_x2_ = r.read_u8();
    clip_y1_ = r.read_u8(); clip_y2_ = r.read_u8();
}
