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
    clip_y2_          = 255;

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

void SpriteEngine::render_scanline(uint32_t* dst, int y,
                                   const PaletteManager& palette) const
{
    if (!sprites_visible_)
        return;

    // Line-occupied tracker for collision detection (320 pixels max).
    bool line_occupied[DISPLAY_WIDTH] = {};

    if (zero_on_top_) {
        // Sprite 0 on top: render from 127 down to 0 so sprite 0 is last
        // (overwrites everything).  But collision is still detected.
        for (int i = NUM_SPRITES - 1; i >= 0; --i) {
            render_sprite_scanline(dst, sprites_[i], y, palette, line_occupied);
        }
    } else {
        // Default: higher-index sprites on top (rendered last, overwrite).
        for (int i = 0; i < NUM_SPRITES; ++i) {
            render_sprite_scanline(dst, sprites_[i], y, palette, line_occupied);
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

    // Y offset = scanline - sprite_y.  For scale x1, sprite is 16 pixels tall.
    // The offset must be in range [0, 15] for the sprite to be on this scanline.
    int y_offset = y - spr_y;

    // Handle 9-bit Y wrapping: if sprite Y > 256, it wraps around.
    // The VHDL uses 9-bit subtraction; we simulate by checking modular distance.
    if (y_offset < 0)
        y_offset += 512;
    if (y_offset < 0 || y_offset >= SPRITE_SIZE)
        return;

    // Apply Y mirror: complement the row index within the sprite.
    // From VHDL: spr_y_index <= y_offset(3:0) when ymirror='0' else not(y_offset(3:0))
    int y_index = spr.y_mirror() ? (SPRITE_SIZE - 1 - y_offset) : y_offset;

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

    // Determine clip window bounds and border offset.
    //
    // VHDL reference (sprites.vhd lines 1048-1059):
    //   When over_border='1': clip coords map directly to pixel positions.
    //     x_s = clip_x1 << 1,  x_e = (clip_x2 << 1) | 1
    //     y_s = clip_y1,        y_e = clip_y2
    //   When over_border='0': clip coords are shifted by +32 into display area.
    //     x_s = (('0' & clip_x1(7:5)) + 1) & clip_x1(4:0)  -- adds ~32
    //     (similarly for x_e, y_s, y_e)
    //
    // In non-over-border mode, sprite coordinates are in display space (X=0..255,
    // Y=0..191), but the framebuffer is 320 pixels wide with 32px borders.  The
    // VHDL adds a +32 offset to the sprite position internally so that X=0 maps
    // to the left edge of the display area (hcounter=32).  We replicate this by
    // adding BORDER_OFFSET to the sprite X when computing buffer positions, and
    // to the scanline Y when doing the clip check.

    static constexpr int BORDER_OFFSET = 32;

    int clip_xs, clip_xe, clip_ys, clip_ye;
    int x_offset;   // added to sprite X for buffer positioning
    int y_clip_adj; // added to y for clip check (display→buffer coords)

    if (over_border_) {
        clip_xs = clip_x1_ * 2;
        clip_xe = clip_x2_ * 2 + 1;
        clip_ys = clip_y1_;
        clip_ye = clip_y2_;
        x_offset   = 0;
        y_clip_adj = 0;
    } else {
        // Non-over-border: clip window shifted into buffer space (+32).
        // From VHDL: (('0' & val(7:5)) + 1) & val(4:0)
        clip_xs = ((((clip_x1_ >> 5) & 0x07) + 1) << 5) | (clip_x1_ & 0x1F);
        clip_xe = ((((clip_x2_ >> 5) & 0x07) + 1) << 5) | (clip_x2_ & 0x1F);
        clip_ys = ((((clip_y1_ >> 5) & 0x07) + 1) << 5) | (clip_y1_ & 0x1F);
        clip_ye = ((((clip_y2_ >> 5) & 0x07) + 1) << 5) | (clip_y2_ & 0x1F);
        // Sprite coords are display-relative; offset to buffer space.
        x_offset   = BORDER_OFFSET;
        y_clip_adj = BORDER_OFFSET;
    }

    // Check Y clip (convert display-space y to buffer-space for comparison)
    if ((y + y_clip_adj) < clip_ys || (y + y_clip_adj) > clip_ye)
        return;

    // Iterate over 16 pixel columns of the sprite.
    for (int col = 0; col < SPRITE_SIZE; ++col) {
        int screen_x = spr_x + x_offset + col;

        // Wrap X within 9-bit space (0-511), but only draw if on-screen (0-319).
        screen_x &= 0x1FF;
        if (screen_x >= DISPLAY_WIDTH)
            continue;

        // Check X clip
        if (screen_x < clip_xs || screen_x > clip_xe)
            continue;

        // Determine pixel coordinates within the pattern.
        int px, py;
        if (!rotated) {
            px = x_mirror_eff ? (SPRITE_SIZE - 1 - col) : col;
            py = y_index;
        } else {
            // Rotation: swap row/col in pattern address.
            // From VHDL: addr_start = pattern & x_index & y_index (when rotated)
            px = x_mirror_eff ? (SPRITE_SIZE - 1 - col) : col;
            py = y_index;
            // Swap: pattern address row = px, col = py
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
