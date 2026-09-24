#include "audio/ay_chip.h"
#include "core/saveable.h"
#include "save/state_desc.h"

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

    // R14 / R15 are the two I/O ports, and their read is NOT the stored
    // byte. VHDL ym2149.vhd:240-249:
    //
    //   when x"E" => if (reg(7)(6) = '0') then O_DA <= port_a_i;
    //                else                      O_DA <= reg(14) and port_a_i;
    //   when x"F" => if (reg(7)(7) = '0') then O_DA <= port_b_i;
    //                else                      O_DA <= reg(15) and port_b_i;
    //
    // R7 bits 7/6 are the port direction bits: '0' = input, and an input
    // port reads the PIN, not the latch. On the Next both pins are tied to
    // all-ones for all three PSGs (turbosound.vhd:174-176, :229-231,
    // :284-286), so an input-mode read is 0xFF whatever was written, and an
    // output-mode read is `stored AND 0xFF` = the stored byte.
    //
    // GH #201 — jnext returned the stored byte in BOTH directions, so a
    // program that put a port in input mode and probed it (the classic
    // AY-port joystick / peripheral detect) read back its own last write
    // instead of the pulled-up 0xFF the hardware drives. The VHDL case is
    // shared by both AY and YM mode, so this sits above the AY-mode masks.
    constexpr uint8_t kPortPullup = 0xFF;   // turbosound.vhd ties both high
    if (r == 14) {
        return (reg_[7] & 0x40) ? static_cast<uint8_t>(reg_[14] & kPortPullup)
                                : kPortPullup;
    }
    if (r == 15) {
        return (reg_[7] & 0x80) ? static_cast<uint8_t>(reg_[15] & kPortPullup)
                                : kPortPullup;
    }

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

        // VHDL ym2149.vhd:270-272 —
        //     noise_div <= not noise_div;          -- signal assignment
        //     if (noise_div = '1') then            -- reads the OLD value
        //        ena_div_noise <= '1';
        //     end if;
        // The `if` is evaluated against the value `noise_div` held on entry
        // to the process, because the VHDL assignment on the previous line
        // does not take effect until the process suspends. `noise_div`
        // powers on at '0' (ym2149.vhd:106), so `ena_div_noise` fires on the
        // SECOND ena_div pulse and every even one thereafter.
        //
        // Reading the toggled value here instead (the pre-2026-09-24 code)
        // kept the /2 rate but inverted the phase: the noise clock fired on
        // the 1st, 3rd, 5th ena_div. Pinned by AY-43.
        const bool noise_div_prev = noise_div_;
        noise_div_ = !noise_div_;
        if (noise_div_prev) {
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

    // Boundary detection — VHDL ym2149.vhd:361-366.
    //
    //   is_zero <= '1' when env_vol(4 downto 1) = "0000" else '0';
    //   is_bot  <= '1' when is_zero = '1' and env_vol(0) = '0' else '0';   (etc.)
    //
    // These are CONCURRENT assignments from `env_vol`, so inside the clocked
    // shape process at :402-462 they still carry the value `env_vol` had at
    // the start of this step — the step's own assignment does not land until
    // the process ends. They must therefore be computed BEFORE the counter
    // moves. GH #201: they used to be computed after, and the branches were
    // left using VHDL's flag names against post-step values, which put the
    // hold one level off on shapes 4-7/9/13 and INVERTED shapes 11 and 15
    // (both end at the wrong rail) and made the two triangles (10 and 14)
    // lock one level inside the boundary instead of turning round. The
    // steady states below are independently pinned by the shape table
    // spelled out at ym2149.vhd:371-391 (`\___`, `\‾‾‾`, `/‾‾‾`, `/___`),
    // which needs no signal-timing argument at all.
    const uint8_t pre_vol = env_vol_;
    const bool is_zero = ((pre_vol >> 1) & 0x0F) == 0;
    const bool is_ones = ((pre_vol >> 1) & 0x0F) == 0x0F;
    const bool is_bot    = is_zero && ((pre_vol & 1) == 0);
    const bool is_bot_p1 = is_zero && ((pre_vol & 1) == 1);
    const bool is_top_m1 = is_ones && ((pre_vol & 1) == 0);
    const bool is_top    = is_ones && ((pre_vol & 1) == 1);

    // Advance envelope volume — VHDL ym2149.vhd:404-409, guarded by the
    // env_hold value as of this clock edge.
    if (!env_hold_) {
        if (env_inc_) {
            env_vol_ = (env_vol_ + 1) & 0x1F;
        } else {
            env_vol_ = (env_vol_ - 1) & 0x1F;
        }
    }

    // Envelope shape control — VHDL ym2149.vhd:411-462. `env_inc` is read
    // as of the clock edge here too, so snapshot it: the alternate branch
    // below is its only writer and must not see its own result.
    const bool inc_before = env_inc_;
    const uint8_t shape = reg_[13] & 0x0F;

    if ((shape & 0x08) == 0) {
        // C=0: shapes 0-7 — one ramp, then hold. VHDL :412-421.
        // Down holds after the step out of env_vol=1 (steady 0, `\___`);
        // up holds after the step out of env_vol=31, which WRAPS to 0
        // (steady 0, `/___` — the table's "rise then silence").
        if (!inc_before) {
            if (is_bot_p1) env_hold_ = true;
        } else {
            if (is_top) env_hold_ = true;
        }
    } else if (shape & 0x01) {
        // C=1, Hold=1: shapes 9, 11, 13, 15. VHDL :422-443.
        if (!inc_before) {
            if (shape & 0x02) {
                if (is_bot) env_hold_ = true;      // 11 `\‾‾‾` → steady 31
            } else {
                if (is_bot_p1) env_hold_ = true;   //  9 `\___` → steady 0
            }
        } else {
            if (shape & 0x02) {
                if (is_top) env_hold_ = true;      // 15 `/___` → steady 0
            } else {
                if (is_top_m1) env_hold_ = true;   // 13 `/‾‾‾` → steady 31
            }
        }
    } else if (shape & 0x02) {
        // C=1, Alt=1, Hold=0: shapes 10 and 14 — the triangles. VHDL
        // :444-461. One step of dwell at each rail (env_hold is set at the
        // step INTO the rail and cleared at the step out of it), then the
        // direction flips, so the ramp runs forever.
        if (!inc_before) {
            if (is_bot_p1) env_hold_ = true;
            if (is_bot) {
                env_hold_ = false;
                env_inc_  = true;
            }
        } else {
            if (is_top_m1) env_hold_ = true;
            if (is_top) {
                env_hold_ = false;
                env_inc_  = false;
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

// GH #27 S5 — the ONE field list for one chip (design §9.2). Declaration
// order IS the binary stream order, so it must not be disturbed: the
// byte-identity gate (§17.1) pins three of these, plus TurboSound's own
// scalars, as block 20 of the 2 292 965-byte stream.
//
// The three tone generators are §9.4's "array elements inside loops" class.
// They COLLAPSE to one declaration per field rather than expanding to six,
// and the keys name the channel (`tone_cnt_a`) rather than numbering it.
void AyChip::describe_state(jnext::save::StateDesc& d, const char* const* k)
{
    d.boolean(k[0], ay_mode_);
    // The 16 register file bytes: a `d.bytes` and not a `d.blob` — §6.1 gives
    // a peripheral store a ZIP member of its own only from 8 KB up. Its
    // length comes from the DECLARATION (`reg_.size()` is a compile-time
    // `std::array` extent), so no count in the stream can size the write.
    d.bytes(k[1], reg_.data(), reg_.size());
    d.u8(k[2], addr_);
    d.u8(k[3], cnt_div_);
    d.boolean(k[4], noise_div_);
    d.boolean(k[5], ena_div_);
    d.boolean(k[6], ena_div_noise_);
    d.u16(k[7], tone_cnt_[0]);
    d.u16(k[8], tone_cnt_[1]);
    d.u16(k[9], tone_cnt_[2]);
    d.boolean(k[10], tone_op_[0]);
    d.boolean(k[11], tone_op_[1]);
    d.boolean(k[12], tone_op_[2]);
    d.u8(k[13], noise_cnt_);
    d.u32(k[14], poly17_);
    d.boolean(k[15], noise_op_);
    d.u16(k[16], env_cnt_);
    d.boolean(k[17], env_ena_);
    d.boolean(k[18], env_reset_);
    d.u8(k[19], env_vol_);
    d.boolean(k[20], env_inc_);
    d.boolean(k[21], env_hold_);
    d.u8(k[22], out_a_);
    d.u8(k[23], out_b_);
    d.u8(k[24], out_c_);
}
