#include "audio/ay_chip.h"
#include "core/saveable.h"

// Volume tables from VHDL ym2149.vhd — zero volume is actually zero
// (modified for ZX Next, unlike real hardware).

const uint8_t AyChip::vol_table_ay_[16] = {
    0x00, 0x03, 0x04, 0x06, 0x0a, 0x0f, 0x15, 0x22,
    0x28, 0x41, 0x5b, 0x72, 0x90, 0xb5, 0xd7, 0xff
};

const uint8_t AyChip::vol_table_ym_[32] = {
    0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04,
    0x06, 0x07, 0x09, 0x0a, 0x0c, 0x0e, 0x11, 0x13,
    0x17, 0x1b, 0x20, 0x25, 0x2c, 0x35, 0x3e, 0x47,
    0x54, 0x66, 0x77, 0x88, 0xa1, 0xc0, 0xe0, 0xff
};

AyChip::AyChip(uint8_t id) : id_(id & 0x03) { reset(); }

void AyChip::reset()
{
    reg_.fill(0);
    reg_[7] = 0xFF;  // All channels disabled on reset (VHDL: reg(7) <= x"ff")
    addr_ = 0;

    cnt_div_ = 0;
    noise_div_ = false;
    ena_div_ = false;
    ena_div_noise_ = false;

    tone_cnt_.fill(0);
    tone_op_.fill(false);

    noise_cnt_ = 0;
    poly17_ = 0;
    noise_op_ = false;

    env_cnt_ = 0;
    env_ena_ = false;
    env_reset_ = false;
    env_vol_ = 0;
    env_inc_ = false;
    env_hold_ = false;

    out_a_ = 0;
    out_b_ = 0;
    out_c_ = 0;
}

void AyChip::select_register(uint8_t addr)
{
    addr_ = addr & 0x1F;
}

void AyChip::write_data(uint8_t val)
{
    if (addr_ & 0x10) return;  // Only registers 0-15 writable

    reg_[addr_ & 0x0F] = val;

    // Writing to register 13 triggers envelope reset
    if ((addr_ & 0x0F) == 0x0D) {
        env_reset_ = true;
    }
}

uint8_t AyChip::read_data(bool reg_mode) const
{
    if (reg_mode) {
        // Return AY_ID (2 bits) & '0' & addr (5 bits)
        return (id_ << 6) | (addr_ & 0x1F);
    }

    // In YM mode, registers 16-31 return 0xFF
    if ((addr_ & 0x10) && !ay_mode_) {
        return 0xFF;
    }

    uint8_t r = addr_ & 0x0F;

    // In AY mode, upper bits of certain registers are masked
    // (registers with fewer than 8 meaningful bits)
    if (ay_mode_) {
        switch (r) {
            case 1: case 3: case 5:
                return reg_[r] & 0x0F;
            case 6: case 8: case 9: case 10:
                return reg_[r] & 0x1F;
            case 13:
                return reg_[r] & 0x0F;
            default:
                return reg_[r];
        }
    }
    return reg_[r];
}

// ---------------------------------------------------------------------------
// Internal timing helpers
// ---------------------------------------------------------------------------

uint16_t AyChip::tone_period(int ch) const
{
    // Tone period: reg[2*ch+1](3:0) & reg[2*ch] = 12-bit value
    return (static_cast<uint16_t>(reg_[ch * 2 + 1] & 0x0F) << 8) | reg_[ch * 2];
}

uint16_t AyChip::tone_comp(int ch) const
{
    uint16_t freq = tone_period(ch);
    // VHDL: comp = freq - 1 when freq[11:1] != 0, else 0
    if ((freq >> 1) != 0) return freq - 1;
    return 0;
}

uint8_t AyChip::noise_period() const { return reg_[6] & 0x1F; }

uint8_t AyChip::noise_comp() const
{
    uint8_t p = noise_period();
    // VHDL: comp = p - 1 when p[4:1] != 0, else 0
    if ((p >> 1) != 0) return p - 1;
    return 0;
}

uint16_t AyChip::env_period() const
{
    return (static_cast<uint16_t>(reg_[12]) << 8) | reg_[11];
}

uint16_t AyChip::env_comp() const
{
    uint16_t freq = env_period();
    if ((freq >> 1) != 0) return freq - 1;
    return 0;
}

// ---------------------------------------------------------------------------
// Tick — advance one PSG clock-enable cycle
// ---------------------------------------------------------------------------

void AyChip::tick()
{
    // Handle envelope reset (deferred from write)
    if (env_reset_) {
        env_cnt_ = 0;
        env_ena_ = true;

        // Load initial envelope state based on attack bit
        if ((reg_[13] & 0x04) == 0) {
            // Attack = 0: start at top, count down
            env_vol_ = 31;
            env_inc_ = false;
        } else {
            // Attack = 1: start at bottom, count up
            env_vol_ = 0;
            env_inc_ = true;
        }
        env_hold_ = false;
        env_reset_ = false;
    }

    update_divider();

    if (ena_div_) {
        update_tone_generators();
        update_envelope();
    }

    if (ena_div_noise_) {
        update_noise_generator();
    }

    update_output();
}

void AyChip::update_divider()
{
    // VHDL: I_SEL_L = '1' (AY compatible), so reload = "0111" = 7
    // Divides PSG clock enable by 8
    ena_div_ = false;
    ena_div_noise_ = false;

    if (cnt_div_ == 0) {
        cnt_div_ = 7;  // Reload: (not I_SEL_L) & "111" with I_SEL_L=1 → "0111"
        ena_div_ = true;

        noise_div_ = !noise_div_;
        if (noise_div_) {
            ena_div_noise_ = true;
        }
    } else {
        cnt_div_--;
    }
}

void AyChip::update_tone_generators()
{
    for (int i = 0; i < 3; i++) {
        uint16_t comp = tone_comp(i);
        if (tone_cnt_[i] >= comp) {
            tone_cnt_[i] = 0;
            tone_op_[i] = !tone_op_[i];
        } else {
            tone_cnt_[i]++;
        }
    }
}

void AyChip::update_noise_generator()
{
    uint8_t comp = noise_comp();
    if (noise_cnt_ >= comp) {
        noise_cnt_ = 0;
        // 17-bit LFSR: new bit = poly17[0] XOR poly17[2] XOR (poly17 == 0)
        bool poly_zero = (poly17_ == 0);
        bool new_bit = ((poly17_ & 1) ^ ((poly17_ >> 2) & 1) ^ poly_zero) & 1;
        poly17_ = (static_cast<uint32_t>(new_bit) << 16) | (poly17_ >> 1);
    } else {
        noise_cnt_++;
    }
    noise_op_ = (poly17_ & 1) != 0;
}

void AyChip::update_envelope()
{
    // Envelope period counter
    uint16_t comp = env_comp();
    if (env_cnt_ >= comp) {
        env_cnt_ = 0;
        env_ena_ = true;
    } else {
        env_cnt_++;
        env_ena_ = false;
        return;  // No envelope step this tick
    }

    if (!env_ena_) return;

    // Advance envelope volume
    if (!env_hold_) {
        if (env_inc_) {
            env_vol_ = (env_vol_ + 1) & 0x1F;
        } else {
            env_vol_ = (env_vol_ - 1) & 0x1F;
        }
    }

    // Envelope shape control
    // Boundary detection (from VHDL)
    bool is_zero = ((env_vol_ >> 1) & 0x0F) == 0;
    bool is_ones = ((env_vol_ >> 1) & 0x0F) == 0x0F;
    bool is_bot    = is_zero && ((env_vol_ & 1) == 0);
    bool is_bot_p1 = is_zero && ((env_vol_ & 1) == 1);
    bool is_top_m1 = is_ones && ((env_vol_ & 1) == 0);
    bool is_top    = is_ones && ((env_vol_ & 1) == 1);

    uint8_t shape = reg_[13] & 0x0F;

    if ((shape & 0x08) == 0) {
        // C=0: shapes 0-7
        // Attack=0 (\___) or Attack=1 (/___)  — single cycle, then hold
        // VHDL ym2149.vhd:412-421: boundary checks use pre-update vol.
        // C++ checks post-update, so shift by one step:
        //   VHDL is_bot_p1 (pre-update vol=1) → C++ is_bot (post-update vol=0)
        //   VHDL is_top (pre-update vol=31)   → C++ is_top_m1 (post-update vol=30...
        //     no: post-update wraps to 0). Staying with is_top since the C++
        //     check-after-update model holds at the boundary value directly.
        if (!env_inc_) {
            // Counting down: hold at vol=0 (VHDL holds at pre-update vol=1,
            // vol decrements to 0 simultaneously → same steady state)
            if (is_bot) env_hold_ = true;
        } else {
            // Counting up: hold at vol=31
            if (is_top) env_hold_ = true;
        }
    } else if (shape & 0x01) {
        // Hold=1: shapes 9, 11, 13, 15
        if (!env_inc_) {
            // Counting down
            if (shape & 0x02) {
                // Alt=1: shapes 11, 15
                if (is_bot) env_hold_ = true;
            } else {
                // Alt=0: shapes 9, 13
                if (is_bot_p1) env_hold_ = true;
            }
        } else {
            // Counting up
            if (shape & 0x02) {
                // Alt=1: shapes 11, 15
                if (is_top) env_hold_ = true;
            } else {
                // Alt=0: shapes 9, 13
                if (is_top_m1) env_hold_ = true;
            }
        }
    } else if (shape & 0x02) {
        // Alt=1, Hold=0: shapes 10, 14 (\/\/ and /\/\)
        if (!env_inc_) {
            if (is_bot_p1) env_hold_ = true;
            if (is_bot) {
                env_hold_ = false;
                env_inc_ = true;
            }
        } else {
            if (is_top_m1) env_hold_ = true;
            if (is_top) {
                env_hold_ = false;
                env_inc_ = false;
            }
        }
    }
    // else: C=1, Alt=0, Hold=0: shapes 8, 12 (\\\\, ////) — continuous, no hold
}

void AyChip::update_output()
{
    // Channel mixing: (tone_disable OR tone_op) AND (noise_disable OR noise_op)
    bool mixed[3];
    mixed[0] = ((reg_[7] & 0x01) || tone_op_[0]) && ((reg_[7] & 0x08) || noise_op_);
    mixed[1] = ((reg_[7] & 0x02) || tone_op_[1]) && ((reg_[7] & 0x10) || noise_op_);
    mixed[2] = ((reg_[7] & 0x04) || tone_op_[2]) && ((reg_[7] & 0x20) || noise_op_);

    // Compute 5-bit volume index for each channel
    uint8_t vol[3] = {0, 0, 0};
    for (int i = 0; i < 3; i++) {
        if (mixed[i]) {
            uint8_t vol_reg = reg_[8 + i];
            if (vol_reg & 0x10) {
                // Envelope mode
                vol[i] = env_vol_ & 0x1F;
            } else {
                // Fixed volume: 4-bit mapped to 5-bit (VHDL: reg(3:0) & "1")
                uint8_t fixed = vol_reg & 0x0F;
                if (fixed == 0) {
                    vol[i] = 0;
                } else {
                    vol[i] = (fixed << 1) | 1;
                }
            }
        }
    }

    // Look up volume table
    if (ay_mode_) {
        // AY mode: use upper 4 bits of 5-bit index
        out_a_ = vol_table_ay_[vol[0] >> 1];
        out_b_ = vol_table_ay_[vol[1] >> 1];
        out_c_ = vol_table_ay_[vol[2] >> 1];
    } else {
        // YM mode: use full 5-bit index
        out_a_ = vol_table_ym_[vol[0]];
        out_b_ = vol_table_ym_[vol[1]];
        out_c_ = vol_table_ym_[vol[2]];
    }
}

void AyChip::save_state(StateWriter& w) const
{
    w.write_bool(ay_mode_);
    w.write_bytes(reg_.data(), reg_.size());
    w.write_u8(addr_);
    w.write_u8(cnt_div_);
    w.write_bool(noise_div_);
    w.write_bool(ena_div_);
    w.write_bool(ena_div_noise_);
    for (int i = 0; i < 3; ++i) w.write_u16(tone_cnt_[i]);
    for (int i = 0; i < 3; ++i) w.write_bool(tone_op_[i]);
    w.write_u8(noise_cnt_);
    w.write_u32(poly17_);
    w.write_bool(noise_op_);
    w.write_u16(env_cnt_);
    w.write_bool(env_ena_);
    w.write_bool(env_reset_);
    w.write_u8(env_vol_);
    w.write_bool(env_inc_);
    w.write_bool(env_hold_);
    w.write_u8(out_a_);
    w.write_u8(out_b_);
    w.write_u8(out_c_);
}

void AyChip::load_state(StateReader& r)
{
    ay_mode_ = r.read_bool();
    r.read_bytes(reg_.data(), reg_.size());
    addr_           = r.read_u8();
    cnt_div_        = r.read_u8();
    noise_div_      = r.read_bool();
    ena_div_        = r.read_bool();
    ena_div_noise_  = r.read_bool();
    for (int i = 0; i < 3; ++i) tone_cnt_[i] = r.read_u16();
    for (int i = 0; i < 3; ++i) tone_op_[i]  = r.read_bool();
    noise_cnt_  = r.read_u8();
    poly17_     = r.read_u32();
    noise_op_   = r.read_bool();
    env_cnt_    = r.read_u16();
    env_ena_    = r.read_bool();
    env_reset_  = r.read_bool();
    env_vol_    = r.read_u8();
    env_inc_    = r.read_bool();
    env_hold_   = r.read_bool();
    out_a_      = r.read_u8();
    out_b_      = r.read_u8();
    out_c_      = r.read_u8();
}
