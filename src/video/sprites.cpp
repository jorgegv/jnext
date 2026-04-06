#include "video/sprites.h"
#include "video/palette.h"
#include "core/log.h"

// ---------------------------------------------------------------------------
// SpriteAttr::y() — decode 9-bit Y coordinate
// ---------------------------------------------------------------------------
//
// From VHDL: spr_y8 <= '0' when sprite_attr_3(6) = '0' else spr_cur_attr_4(0);
// The Y MSB only applies when the extended flag (byte3 bit 6) is set.

int SpriteEngine::SpriteAttr::y() const
{
    int y_msb = extended() ? (byte4 & 0x01) : 0;
    return (y_msb << 8) | byte1;
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void SpriteEngine::reset()
{
    for (auto& s : sprites_) {
        s.byte0 = s.byte1 = s.byte2 = s.byte3 = s.byte4 = 0;
    }
    std::memset(pattern_ram_, 0, sizeof(pattern_ram_));

    attr_slot_        = 0;
    attr_byte_        = 0;
    pattern_offset_   = 0;
    pattern_slot_msb_ = 0;

    sprites_visible_  = false;
    over_border_      = false;
    zero_on_top_      = false;

    clip_x1_          = 0;
    clip_x2_          = 255;
    clip_y1_          = 0;
    clip_y2_          = 0xBF;  // VHDL default: 191

    collision_        = false;
    max_sprites_      = false;
}

// ---------------------------------------------------------------------------
// Port 0x303B write — sprite slot select
// ---------------------------------------------------------------------------
//
// From VHDL:
//   attr_index <= cpu_d_i(6:0) & "000"
//   pattern_index <= cpu_d_i(5:0) & cpu_d_i(7) & "0000000"
// So bits 6:0 select sprite number, bit 7 is pattern MSB.

void SpriteEngine::write_slot_select(uint8_t val)
{
    attr_slot_  = val & 0x7F;
    attr_byte_  = 0;

    // Pattern offset: sprite_number(5:0) concatenated with bit 7, then 7 zero bits
    // = pattern slot × 256 bytes, with bit 7 as N6 extension.
    // In hardware this gives pattern_index = {N5:N0, bit7, 0000000}
    // which is a 14-bit address (only lower 14 bits matter for 16KB).
    pattern_slot_msb_ = (val >> 7) & 1;
    pattern_offset_ = static_cast<uint16_t>(
        ((val & 0x3F) << 8) | (pattern_slot_msb_ << 7)) & (PATTERN_RAM_SZ - 1);
}

// ---------------------------------------------------------------------------
// Port 0x303B read — status register
// ---------------------------------------------------------------------------
//
// From VHDL: bits(7:2) = 0, bit 1 = max sprites per line, bit 0 = collision
// Cleared on read.

uint8_t SpriteEngine::read_status()
{
    uint8_t val = 0;
    if (max_sprites_) val |= 0x02;
    if (collision_)   val |= 0x01;
    max_sprites_ = false;
    collision_   = false;
    return val;
}

// ---------------------------------------------------------------------------
// Port 0x57 write — sprite attribute upload (auto-incrementing)
// ---------------------------------------------------------------------------
//
// The VHDL auto-increment logic:
//   - Bytes 0-2 always written sequentially
//   - After byte 3: if bit 6 is clear (no extended), skip to next sprite
//   - After byte 4: advance to next sprite
//
// From VHDL:
//   index_inc_attr_by_8 <= '1' when attr_index(2)='1'
//       or (attr_index(2:0)="011" and cpu_d_i(6)='0')
// This means: jump to next sprite when byte index >= 4, or when writing
// byte 3 and the extended bit is 0.

void SpriteEngine::write_attribute(uint8_t val)
{
    SpriteAttr& spr = sprites_[attr_slot_ & 0x7F];

    switch (attr_byte_) {
    case 0: spr.byte0 = val; attr_byte_ = 1; break;
    case 1: spr.byte1 = val; attr_byte_ = 2; break;
    case 2: spr.byte2 = val; attr_byte_ = 3; break;
    case 3:
        spr.byte3 = val;
        if (val & 0x40) {
            // Extended: expect 5th byte
            attr_byte_ = 4;
        } else {
            // No extended byte: advance to next sprite
            attr_byte_ = 0;
            attr_slot_ = (attr_slot_ + 1) & 0x7F;
        }
        break;
    case 4:
        spr.byte4 = val;
        attr_byte_ = 0;
        attr_slot_ = (attr_slot_ + 1) & 0x7F;
        break;
    default:
        attr_byte_ = 0;
        break;
    }
}

// ---------------------------------------------------------------------------
// Port 0x5B write — pattern data upload (auto-incrementing)
// ---------------------------------------------------------------------------

void SpriteEngine::write_pattern(uint8_t val)
{
    pattern_ram_[pattern_offset_ & (PATTERN_RAM_SZ - 1)] = val;
    pattern_offset_ = (pattern_offset_ + 1) & (PATTERN_RAM_SZ - 1);
}

// ---------------------------------------------------------------------------
// NextREG 0x34 — sprite attribute slot select (alternative to port 0x303B)
// ---------------------------------------------------------------------------

void SpriteEngine::set_attr_slot(uint8_t val)
{
    attr_slot_ = val & 0x7F;
    attr_byte_ = 0;
    pattern_slot_msb_ = (val >> 7) & 1;
}

// ---------------------------------------------------------------------------
// NextREG 0x75-0x79 — direct sprite attribute byte writes
// ---------------------------------------------------------------------------

void SpriteEngine::write_attr_byte(uint8_t byte_idx, uint8_t val)
{
    if (byte_idx > 4) return;

    SpriteAttr& spr = sprites_[attr_slot_ & 0x7F];
    switch (byte_idx) {
    case 0: spr.byte0 = val; break;
    case 1: spr.byte1 = val; break;
    case 2: spr.byte2 = val; break;
    case 3: spr.byte3 = val; break;
    case 4: spr.byte4 = val; break;
    }

    // Auto-increment sprite index after the last written byte.
    // VHDL mirror_inc_i fires after writing attribute bytes.
    if (byte_idx == 4 || (byte_idx == 3 && !(val & 0x40))) {
        attr_slot_ = (attr_slot_ + 1) & 0x7F;
    }
}

// ---------------------------------------------------------------------------
// Render one scanline of sprites
// ---------------------------------------------------------------------------
//
// The hardware renders sprites into a line buffer.  Earlier sprites are drawn
// first; later sprites overwrite unless zero_on_top is set (in which case
// sprite 0 has highest priority — achieved by not overwriting occupied pixels).
//
// Collision is detected when a non-transparent pixel is written to a position
// already occupied by a previous sprite's non-transparent pixel.

// ---------------------------------------------------------------------------
// Anchor state management for composite sprite chains
// ---------------------------------------------------------------------------

void SpriteEngine::update_anchor(AnchorState& anchor, const SpriteAttr& spr)
{
    // Every non-relative sprite updates the anchor state.
    // Type 1 anchors (byte4 bit 5) inherit mirror/rotate/scale to relatives.
    bool type1 = spr.is_anchor_type1();

    anchor.type1      = type1;
    anchor.h4bit      = spr.extended() && ((spr.byte4 & 0x80) != 0);
    anchor.visible    = spr.visible();
    anchor.x          = spr.x();
    anchor.y          = spr.y();
    anchor.pattern    = spr.pattern_7bit();
    anchor.pal_offset = spr.palette_offset();

    if (type1) {
        anchor.rotate   = spr.rotate();
        anchor.x_mirror = spr.x_mirror();
        anchor.y_mirror = spr.y_mirror();
        anchor.x_scale  = spr.x_scale();
        anchor.y_scale  = spr.y_scale();
    } else {
        anchor.rotate   = false;
        anchor.x_mirror = false;
        anchor.y_mirror = false;
        anchor.x_scale  = 0;
        anchor.y_scale  = 0;
    }
}

SpriteEngine::SpriteAttr SpriteEngine::resolve_relative(const SpriteAttr& rel,
                                                         const AnchorState& anchor)
{
    SpriteAttr eff;

    // --- Offset transformation pipeline ---
    // Stage 1: rotation swap — if anchor rotates, swap X/Y offsets
    int8_t raw_x = static_cast<int8_t>(rel.byte0);
    int8_t raw_y = static_cast<int8_t>(rel.byte1);

    int8_t off_x = anchor.rotate ? raw_y : raw_x;
    int8_t off_y = anchor.rotate ? raw_x : raw_y;

    // Stage 2: mirror negation
    // X: negate if (anchor_rotate XOR anchor_xmirror) = 1
    if (anchor.rotate ^ anchor.x_mirror)
        off_x = -off_x;
    // Y: negate if anchor_ymirror = 1
    if (anchor.y_mirror)
        off_y = -off_y;

    // Stage 3: scale multiplication (signed, left shift)
    int rel_x2 = static_cast<int>(off_x) << anchor.x_scale;
    int rel_y2 = static_cast<int>(off_y) << anchor.y_scale;

    // Stage 4: add to anchor position (9-bit)
    int final_x = (anchor.x + rel_x2) & 0x1FF;
    int final_y = (anchor.y + rel_y2) & 0x1FF;

    // --- Palette offset ---
    // attr2 bit 0: 0 = use relative's palette directly, 1 = add to anchor's
    uint8_t pal;
    if (rel.byte2 & 0x01) {
        pal = (rel.palette_offset() + anchor.pal_offset) & 0x0F;
    } else {
        pal = rel.palette_offset();
    }

    // --- Mirror/rotate for type 1 ---
    bool rel_xm, rel_ym, rel_rot;
    if (anchor.rotate) {
        // When anchor has rotation, relative's mirror bits are remapped
        rel_xm = ((rel.byte2 >> 2) & 1) ^ ((rel.byte2 >> 1) & 1);  // ymirror XOR rotate
        rel_ym = ((rel.byte2 >> 3) & 1) ^ ((rel.byte2 >> 1) & 1);  // xmirror XOR rotate
    } else {
        rel_xm = rel.x_mirror();
        rel_ym = rel.y_mirror();
    }
    rel_rot = rel.rotate();

    bool eff_xm, eff_ym, eff_rot;
    if (anchor.type1) {
        // Type 1: XOR relative's transforms with anchor's
        eff_xm  = anchor.x_mirror ^ rel_xm;
        eff_ym  = anchor.y_mirror ^ rel_ym;
        eff_rot = anchor.rotate   ^ rel_rot;
    } else {
        // Type 0: use relative's own transforms directly
        eff_xm  = rel.x_mirror();
        eff_ym  = rel.y_mirror();
        eff_rot = rel.rotate();
    }

    // --- Pattern ---
    // For relative sprites, byte4(5) holds N6 (not byte4(6) which is part of "01" marker)
    uint8_t rel_n6 = (rel.byte4 >> 5) & 1;
    uint8_t rel_pattern = (rel.pattern_base() << 1) | rel_n6;
    // attr4 bit 0: 1 = add anchor pattern to relative's pattern
    if (rel.byte4 & 0x01) {
        rel_pattern = (rel_pattern + anchor.pattern) & 0x7F;
    }

    // --- Scale ---
    uint8_t eff_xscale, eff_yscale;
    if (anchor.type1) {
        // Type 1: inherit anchor's scale
        eff_xscale = anchor.x_scale;
        eff_yscale = anchor.y_scale;
    } else {
        // Type 0: use relative's own scale
        eff_xscale = rel.x_scale();
        eff_yscale = rel.y_scale();
    }

    // --- Reconstruct effective SpriteAttr bytes ---
    eff.byte0 = final_x & 0xFF;
    eff.byte1 = final_y & 0xFF;
    eff.byte2 = static_cast<uint8_t>(
        (pal << 4) |
        (eff_xm  ? 0x08 : 0) |
        (eff_ym  ? 0x04 : 0) |
        (eff_rot ? 0x02 : 0) |
        ((final_x >> 8) & 0x01));
    // Visibility: anchor AND relative must both be visible
    bool eff_vis = anchor.visible && rel.visible();
    eff.byte3 = static_cast<uint8_t>(
        (eff_vis ? 0x80 : 0) |
        0x40 |  // extended flag
        ((rel_pattern >> 1) & 0x3F));
    // H4BIT from anchor; N6 from computed pattern; scale from effective
    eff.byte4 = static_cast<uint8_t>(
        (anchor.h4bit ? 0x80 : 0) |
        ((rel_pattern & 1) << 6) |  // N6
        (eff_xscale << 3) |
        (eff_yscale << 1) |
        ((final_y >> 8) & 0x01));

    return eff;
}

// ---------------------------------------------------------------------------
// Render one scanline of sprites
// ---------------------------------------------------------------------------

void SpriteEngine::render_scanline(uint32_t* dst, int y,
                                   const PaletteManager& palette) const
{
    if (!sprites_visible_)
        return;

    // Pre-resolve anchor chains: compute effective attributes for all sprites.
    // Anchor chain is always evaluated in sprite index order 0-127 regardless
    // of zero_on_top rendering order (VHDL: spr_cur_index increments in S_QUALIFY).
    SpriteAttr effective[NUM_SPRITES];
    AnchorState anchor{};

    for (int i = 0; i < NUM_SPRITES; ++i) {
        const auto& spr = sprites_[i];
        if (spr.is_relative()) {
            effective[i] = resolve_relative(spr, anchor);
        } else {
            effective[i] = spr;
            update_anchor(anchor, spr);
        }
    }

    // Line-occupied tracker for collision detection (320 pixels max).
    bool line_occupied[DISPLAY_WIDTH] = {};

    if (zero_on_top_) {
        for (int i = NUM_SPRITES - 1; i >= 0; --i) {
            render_sprite_scanline(dst, effective[i], y, palette, line_occupied);
        }
    } else {
        for (int i = 0; i < NUM_SPRITES; ++i) {
            render_sprite_scanline(dst, effective[i], y, palette, line_occupied);
        }
    }
}

// ---------------------------------------------------------------------------
// Render a single sprite's contribution to one scanline
// ---------------------------------------------------------------------------

void SpriteEngine::render_sprite_scanline(uint32_t* dst, const SpriteAttr& spr,
                                          int y, const PaletteManager& palette,
                                          bool* line_occupied) const
{
    if (!spr.visible())
        return;

    // Compute sprite Y coordinate (9-bit) and check scanline overlap.
    int spr_y = spr.y();
    int spr_x = spr.x();

    // Scale factors: 0=1x, 1=2x, 2=4x, 3=8x
    int x_scale_shift = spr.x_scale();
    int y_scale_shift = spr.y_scale();
    int scaled_height = SPRITE_SIZE << y_scale_shift;
    int scaled_width  = SPRITE_SIZE << x_scale_shift;

    // Y offset = scanline - sprite_y.  Must be in [0, scaled_height).
    int y_offset = y - spr_y;

    // Handle 9-bit Y wrapping: if sprite Y > 256, it wraps around.
    // The VHDL uses 9-bit subtraction; we simulate by checking modular distance.
    if (y_offset < 0)
        y_offset += 512;
    if (y_offset < 0 || y_offset >= scaled_height)
        return;

    // Divide Y offset by scale factor to get pattern row (0-15).
    // From VHDL: arithmetic right-shift by scale amount.
    int pattern_row = y_offset >> y_scale_shift;

    // Apply Y mirror: complement the row index within the sprite.
    // From VHDL: spr_y_index <= y_offset(3:0) when ymirror='0' else not(y_offset(3:0))
    int y_index = spr.y_mirror() ? (SPRITE_SIZE - 1 - pattern_row) : pattern_row;

    // Rotation swaps X and Y in the pattern address.
    bool rotated = spr.rotate();

    // X mirror is inverted by rotation.
    // From VHDL: spr_x_mirr_eff <= attr2(3) xor attr2(1)
    bool x_mirror_eff = spr.x_mirror() ^ spr.rotate();

    // Pattern index (7-bit): N5:N0 form the base, N6 from byte4 if extended+4bit.
    // In 8-bit mode, pattern_7bit() gives {N5:N0, 0} (N6 is 0).
    // In 4-bit mode, pattern_7bit() gives {N5:N0, N6}.
    uint8_t pattern = spr.pattern_7bit();

    bool is_4bit = spr.is_4bit();
    uint8_t pal_offset = spr.palette_offset();
    uint8_t transp = palette.sprite_transparency();

    // Determine clip window bounds.
    //
    // VHDL reference (sprites.vhd lines 1048-1059, zxula_timing.vhd):
    //   Sprite coordinates are in absolute framebuffer space (whc domain):
    //     whc 0    = left border start
    //     whc 32   = display area start
    //     whc 287  = display area end
    //     whc 319  = right border end
    //   There is NO automatic +32 offset — sprite X maps directly to the
    //   line buffer address (spr_cur_hcount <= spr_cur_x).
    //
    //   When over_border='1': clip coords map directly to pixel positions.
    //     x_s = clip_x1 << 1,  x_e = (clip_x2 << 1) | 1
    //     y_s = clip_y1,        y_e = clip_y2
    //   When over_border='0': clip coords are shifted by +32 into display area.
    //     x_s = (('0' & clip_x1(7:5)) + 1) & clip_x1(4:0)
    //     This produces x_s=32 for clip_x1=0, clipping sprites in the border.

    int clip_xs, clip_xe, clip_ys, clip_ye;

    if (over_border_) {
        clip_xs = clip_x1_ * 2;
        clip_xe = clip_x2_ * 2 + 1;
        clip_ys = clip_y1_;
        clip_ye = clip_y2_;
    } else {
        // Non-over-border: clip window shifted into buffer space (+32).
        // From VHDL: (('0' & val(7:5)) + 1) & val(4:0)
        clip_xs = ((((clip_x1_ >> 5) & 0x07) + 1) << 5) | (clip_x1_ & 0x1F);
        clip_xe = ((((clip_x2_ >> 5) & 0x07) + 1) << 5) | (clip_x2_ & 0x1F);
        clip_ys = ((((clip_y1_ >> 5) & 0x07) + 1) << 5) | (clip_y1_ & 0x1F);
        clip_ye = ((((clip_y2_ >> 5) & 0x07) + 1) << 5) | (clip_y2_ & 0x1F);
    }

    // Check Y clip.
    // VHDL: non-over-border mode also hardcodes vcounter_i < 224 (display bottom).
    if (y < clip_ys || y > clip_ye)
        return;
    if (!over_border_ && y >= 224)
        return;

    // Iterate over scaled width of the sprite.
    // Each screen pixel maps to a pattern column via division by scale factor.
    for (int col = 0; col < scaled_width; ++col) {
        int screen_x = spr_x + col;

        // Wrap X within 9-bit space (0-511), but only draw if on-screen (0-319).
        screen_x &= 0x1FF;
        if (screen_x >= DISPLAY_WIDTH)
            continue;

        // Check X clip
        if (screen_x < clip_xs || screen_x > clip_xe)
            continue;

        // Divide screen column by X scale to get pattern column (0-15).
        int pattern_col = col >> x_scale_shift;

        // Determine pixel coordinates within the pattern.
        int px, py;
        if (!rotated) {
            px = x_mirror_eff ? (SPRITE_SIZE - 1 - pattern_col) : pattern_col;
            py = y_index;
        } else {
            // Rotation: swap row/col in pattern address.
            px = x_mirror_eff ? (SPRITE_SIZE - 1 - pattern_col) : pattern_col;
            py = y_index;
            int tmp = px;
            px = py;
            py = tmp;
        }

        uint8_t pixel_val;

        if (!is_4bit) {
            // 8-bit colour mode.
            // Pattern address: pattern(5:0) & row(3:0) & col(3:0)
            // pattern_7bit gives {N5:N0, 0}, so shift right by 1 to get the 6-bit base.
            uint16_t addr = static_cast<uint16_t>(
                ((pattern >> 1) << 8) | (py << 4) | px);
            pixel_val = read_pattern(addr);

            // Check transparency (raw pixel value before palette offset).
            if (pixel_val == transp)
                continue;

            // Apply palette offset to high nibble.
            // From VHDL: (spr_pat_data(7:4) + spr_cur_paloff) & spr_pat_data(3:0)
            pixel_val = static_cast<uint8_t>(
                (((pixel_val >> 4) + pal_offset) << 4) | (pixel_val & 0x0F));
        } else {
            // 4-bit colour mode.
            // Pattern address is halved: pattern(5:1) & N6 & row(3:0) & col(3:1)
            // Each byte holds two 4-bit pixels.
            uint16_t addr = static_cast<uint16_t>(
                ((pattern >> 1) << 7) | (py << 3) | (px >> 1));
            uint8_t raw = read_pattern(addr);

            // Select high or low nibble based on column LSB.
            // From VHDL: spr_nibble_data = data(7:4) when addr(0)=0 else data(3:0)
            uint8_t nibble = (px & 1) == 0 ? (raw >> 4) : (raw & 0x0F);

            // Check transparency (compare 4-bit nibble against low 4 bits of transp).
            if (nibble == (transp & 0x0F))
                continue;

            // Final pixel = palette_offset & nibble.
            // From VHDL: spr_cur_paloff & spr_nibble_data(3:0)
            pixel_val = static_cast<uint8_t>((pal_offset << 4) | nibble);
        }

        // Collision detection: if this pixel position is already occupied by
        // another sprite's non-transparent pixel, set the collision flag.
        if (line_occupied[screen_x]) {
            collision_ = true;
        }
        line_occupied[screen_x] = true;

        // Write the pixel to the output buffer.
        dst[screen_x] = palette.sprite_colour(pixel_val);
    }
}

void SpriteEngine::debug_log_sprite0() const
{
    const auto& s = sprites_[0];
    Log::video()->info("SPR0: x={} y={} vis={} pat={} bytes=[{:02x},{:02x},{:02x},{:02x},{:02x}] "
                       "global_vis={} over_border={} attr_slot={} attr_byte={}",
                       s.x(), s.y(), s.visible(), s.pattern_base(),
                       s.byte0, s.byte1, s.byte2, s.byte3, s.byte4,
                       sprites_visible_, over_border_, attr_slot_, attr_byte_);
}

// ---------------------------------------------------------------------------
// Debug / introspection
// ---------------------------------------------------------------------------

uint8_t SpriteEngine::read_attr_byte(uint8_t sprite_idx, uint8_t byte_idx) const
{
    if (sprite_idx >= NUM_SPRITES || byte_idx >= 5)
        return 0;
    const auto& s = sprites_[sprite_idx];
    switch (byte_idx) {
        case 0: return s.byte0;
        case 1: return s.byte1;
        case 2: return s.byte2;
        case 3: return s.byte3;
        case 4: return s.byte4;
        default: return 0;
    }
}

SpriteEngine::SpriteInfo SpriteEngine::get_sprite_info(uint8_t idx) const
{
    SpriteInfo info{};
    if (idx >= NUM_SPRITES)
        return info;
    const auto& s = sprites_[idx];
    info.x              = s.x();
    info.y              = s.y();
    info.pattern        = s.extended() ? s.pattern_7bit() : s.pattern_base();
    info.palette_offset = s.palette_offset();
    info.visible        = s.visible();
    info.x_mirror       = s.x_mirror();
    info.y_mirror       = s.y_mirror();
    info.rotate         = s.rotate();
    info.is_4bit        = s.is_4bit();
    info.x_scale        = s.x_scale();
    info.y_scale        = s.y_scale();
    return info;
}
