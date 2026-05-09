#include "nextreg.h"
#include "core/log.h"
#include "core/saveable.h"

NextReg::NextReg() {
    // PASS-5: install FPGA-power-on defaults BEFORE the first reset().
    // The reset() function gates 0x82-0x85 / 0x86-0x89 on the reset_type
    // bits stored in 0x85 / 0x89, and preserves 0x7F/0x80/0x8C across
    // resets. With `regs_{}` value-initialised to all zeros, those
    // gating reads would all see 0 and take the wrong path on the first
    // reset. Mirror the VHDL signal initialisers verbatim:
    //   nr_82-84_internal_port_enable        := (others=>'1')   (:1226-1228)
    //   nr_85_internal_port_enable           := (others=>'1')   (:1229)
    //   nr_85_internal_port_reset_type       := '1'             (:1230)
    //   nr_86-88_bus_port_enable             := (others=>'1')   (:1231-1233)
    //   nr_89_bus_port_enable                := (others=>'1')   (:1234)
    //   nr_89_bus_port_reset_type            := '1'             (:1235)
    //   nr_7f_user_register_0                := X"FF"           (:1216)
    regs_[0x82] = 0xFF;
    regs_[0x83] = 0xFF;
    regs_[0x84] = 0xFF;
    regs_[0x85] = 0x8F;
    regs_[0x86] = 0xFF;
    regs_[0x87] = 0xFF;
    regs_[0x88] = 0xFF;
    regs_[0x89] = 0x8F;
    regs_[0x7F] = 0xFF;
    reset();
}

void NextReg::reset() {
    // VHDL zxnext.vhd:5052-5057: on soft reset, NR 0x82-0x85 reload to
    // 0xFF only when reset_type (NR 0x85 bit 7) is 1. When reset_type=0,
    // the previous values are preserved across the reset.
    const bool reset_type_1 = (regs_[0x85] & 0x80) != 0;
    const uint8_t saved_82 = regs_[0x82];
    const uint8_t saved_83 = regs_[0x83];
    const uint8_t saved_84 = regs_[0x84];
    const uint8_t saved_85 = regs_[0x85];
    // VHDL zxnext.vhd:5061-5067: the bus-port enable group nr_86/87/88/89
    // is reset to 0xFF (and 0x8F for 0x89) ONLY when nr_89_bus_port_reset_type
    // (NR 0x89 bit 7) is 0. With bit 7 = 1 (the FPGA power-on default per
    // :1234-1235), the bytes survive the reset. PASS-5 fix — pre-pass-5 the
    // emulator unconditionally reset 0x86/0x87/0x88/0x89, an approximation
    // that breaks software which sets bit 7 of NR 0x89 to lock its bus-port
    // enable mask across resets. Note the polarity is INVERTED versus the
    // 0x82-0x84/0x85 group above: bit 7 = 1 here keeps the value (no reset).
    const bool bus_reset_type_0 = (regs_[0x89] & 0x80) == 0;
    const uint8_t saved_86 = regs_[0x86];
    const uint8_t saved_87 = regs_[0x87];
    const uint8_t saved_88 = regs_[0x88];
    const uint8_t saved_89 = regs_[0x89];

    // VHDL zxnext.vhd:2185-2186 — on reset, NR 0x80 expbus byte folds the
    // low nibble into the high nibble: `nr_80_expbus(7:4) <= nr_80_expbus(3:0)`.
    // Bits 3:0 (FDC enable, expbus-clken, NMI-debounce-disable bit, ULA
    // override) survive across reset; bits 7:4 (expbus-en, romcs replace,
    // disable IO/MEM) are reloaded from bits 3:0. PASS-5 fix — pre-pass-5
    // the entire byte was zeroed by `regs_.fill(0)` below.
    const uint8_t saved_80_lo = static_cast<uint8_t>(regs_[0x80] & 0x0F);
    const uint8_t computed_80 = static_cast<uint8_t>((saved_80_lo << 4) | saved_80_lo);

    // VHDL zxnext.vhd:1216 — NR 0x7F (`nr_7f_user_register_0`) has no
    // reset entry; the value survives both hard and soft reset. The
    // power-on default X"FF" is installed in the constructor after
    // reset() returns; subsequent resets preserve whatever value was
    // last written.
    const uint8_t saved_7f = regs_[0x7F];

    // VHDL zxnext.vhd:2255 — NR 0x8C altrom byte does the same lo→hi
    // nibble fold on reset as NR 0x80. The MMU-side path
    // (Mmu::set_nr_8c / Mmu::reset) already handles this for the MMU's
    // own `nr_8c_reg_` mirror; preserve `regs_[0x8C]` here too so the
    // NextReg cache stays consistent with the MMU mirror.
    const uint8_t saved_8c_lo = static_cast<uint8_t>(regs_[0x8C] & 0x0F);
    const uint8_t computed_8c = static_cast<uint8_t>((saved_8c_lo << 4) | saved_8c_lo);

    // PASS-6 NR 0x06 reset preservation. VHDL zxnext.vhd:1107-1113 declares
    // NR 0x06 as a per-bit signal split, with ONLY two bits in the reset
    // clause at zxnext.vhd:4932-4933:
    //   nr_06_hotkey_cpu_speed_en (bit 7) <= '1'
    //   nr_06_hotkey_5060_en      (bit 5) <= '1'
    // The remaining fields (bit 6 internal_speaker_beep, bit 4
    // button_drive_nmi_en, bit 3 button_m1_nmi_en, bit 2 ps2_mode, bits 1:0
    // psg_mode) have ONLY initial-value declarations (= power-on '0'); they
    // are NOT in the reset block, so they survive both hard and soft reset.
    // Pre-pass-6 jnext unconditionally re-applied 0xA0 here, clobbering any
    // user-set value of bits 4/3 (the Multiface / DivMMC NMI button enables)
    // on every soft reset — software that wrote NR 0x06 to enable an NMI
    // source and then issued NR 0x02 ← 0x01 (soft reset) would silently lose
    // the gate. Mirror the same save/restore pattern used for 0x82-0x85.
    const uint8_t saved_06 = regs_[0x06];
    // Bits 7 and 5 are re-asserted to '1' by the VHDL reset block; the other
    // bits are inherited verbatim from the pre-reset value.
    const uint8_t computed_06 = static_cast<uint8_t>((saved_06 & ~0xA0) | 0xA0);

    regs_.fill(0);
    regs_[0x80] = computed_80;
    regs_[0x7F] = saved_7f;
    regs_[0x8C] = computed_8c;
    // Reset defaults from VHDL / ZX Next documentation
    // Machine ID: JNEXT DEVIATES from VHDL here on purpose.
    //   VHDL: g_machine_id = X"0A" (ZX Spectrum Next Issue 2/4/5 top-level
    //   generic in zxnext_top_issue{2,4,5}.vhd:35).
    //   jnext: 0x08 (HWID_EMULATORS) — the TBBlue-firmware convention so
    //   NextZXOS can take its emulator-aware boot paths (e.g. skip
    //   FPGA-flash-specific behaviour). Reporting 0x0A makes NextZXOS
    //   treat us as real hardware and dive into the config/flashing
    //   flow, which fails for emulator-mounted images.
    //   Covered by test MID-01 in test/nextreg/nextreg_integration_test.cpp.
    regs_[0x00] = 0x08;
    regs_[0x01] = 0x32;  // core version 3.02 (VHDL g_version = X"32")
    regs_[0x03] = 0x00;  // machine type: ZXNext
    // NR 0x05 power-on default: 0x41 per VHDL read formula at zxnext.vhd:5897.
    // VHDL signal defaults: nr_05_joy0 = "001" (Kempston1, :1105),
    // nr_05_joy1 = "000" (Sinclair2, :1106), nr_05_5060 = '0' (:1302),
    // nr_05_scandouble_en = '1' (:1303). Read formula composes these as
    //   joy0[1:0]<<6 | joy1[1:0]<<4 | joy0[2]<<3 | eff_5060<<2 |
    //   joy1[2]<<1  | eff_scandouble = 0x40 | 0x01 = 0x41.
    // NR 0x05 is not cleared on soft reset (no entry in the reset block).
    // G56 cluster A: prior 0x40 missed the scandouble_en default bit.
    regs_[0x05] = 0x41;
    // NR 0x06 — write the preserved-with-bits-7,5-forced-to-'1' value. See
    // PASS-6 comment above for the VHDL rationale. On power-on the
    // constructor-time `regs_{}` value-init means saved_06 == 0, so
    // computed_06 == 0xA0 — matching the pre-pass-6 G125 power-on behaviour.
    // Subsequent resets preserve bits 6/4/3/2/1/0 of NR 0x06 verbatim.
    regs_[0x06] = computed_06;
    regs_[0x07] = 0x00;  // CPU speed: 3.5 MHz
    regs_[0x0B] = 0x01;  // IO mode: VHDL zxnext.vhd:4939-4941 (iomode_0=1 on reset)
    // NR 0x10 power-on default. VHDL zxnext.vhd:1133:
    //   nr_10_coreid <= "00001"  (5 bits, lands in NR 0x10 read at bits 6:2)
    // Read mux at zxnext.vhd:5924: '0' & nr_10_coreid & i_SPKEY_BUTTONS(1:0)
    // → 0_00001_00 = 0x04 with buttons idle (jnext models them as 0). G56.
    regs_[0x10] = 0x04;
    // Sub-version: VHDL g_sub_version = X"03" generic in
    // zxnext_top_issue2.vhd:38 (also issue4/issue5). Read mux at
    // zxnext.vhd:5917-5918 returns g_sub_version verbatim.
    regs_[0x0E] = 0x03;
    // Port-enable registers: VHDL zxnext.vhd:1226-1230, 5052-5057.
    // When reset_type='1' (the power-on default), nr_82-85 enable[3:0]
    // bits reload to ones. The reset_type bit (NR 0x85 bit 7) itself is
    // never touched by the reset block (only by power-on init :1230 and
    // explicit writes :5509), so it always survives across reset_type_1
    // and reset_type_0 paths alike. 0x85 read mux composes
    //   reset_type & "000" & enable[3:0]
    // (zxnext.vhd:6138). When reset_type='0', the enables survive too.
    if (reset_type_1) {
        regs_[0x82] = 0xFF;
        regs_[0x83] = 0xFF;
        regs_[0x84] = 0xFF;
        regs_[0x85] = static_cast<uint8_t>((saved_85 & 0x80) | 0x0F);
    } else {
        regs_[0x82] = saved_82;
        regs_[0x83] = saved_83;
        regs_[0x84] = saved_84;
        // VHDL zxnext.vhd:6138 read mux composes 0x85 as
        //   reset_type & "000" & enable[3:0]
        // (bits 6:4 always read 0). Apply the same pack mask to the
        // stored byte so the cached value never leaks the dropped bits.
        regs_[0x85] = static_cast<uint8_t>(saved_85 & 0x8F);
    }
    // NR 0x86 / NR 0x87 / NR 0x88 / NR 0x89 bus port enables: VHDL
    // zxnext.vhd:1231-1235 — power-on defaults are (others=>'1') for the
    // enable bytes and reset_type='1' (bit 7 of 0x89). Read mux at
    // zxnext.vhd:6140-6150 returns each byte directly (0x89 composes
    // bit7=reset_type | "000" | enable[3:0]).
    //
    // PASS-5 FIX: VHDL zxnext.vhd:5061-5067 gates the reset on
    // `nr_89_bus_port_reset_type='0'` — the INVERSE polarity of the
    // 0x82-0x85 group. With bit 7 = 1 (the power-on default) the values
    // survive the reset; with bit 7 = 0 they reload to the power-on
    // defaults. The reset_type bit itself is never reset (only set at
    // power-on or by an explicit write at :5522), so it survives both
    // paths. Pre-pass-5 the emulator applied unconditional 0xFF/0x8F
    // here, clobbering software-locked bus-port masks on every reset.
    if (bus_reset_type_0) {
        regs_[0x86] = 0xFF;
        regs_[0x87] = 0xFF;
        regs_[0x88] = 0xFF;
        regs_[0x89] = static_cast<uint8_t>((saved_89 & 0x80) | 0x0F);
    } else {
        regs_[0x86] = saved_86;
        regs_[0x87] = saved_87;
        regs_[0x88] = saved_88;
        // VHDL zxnext.vhd:6150 read mux composes 0x89 the same way as 0x85:
        //   reset_type & "000" & enable[3:0]. Mask to 0x8F.
        regs_[0x89] = static_cast<uint8_t>(saved_89 & 0x8F);
    }
    // VHDL zxnext.vhd:4594-4596 — nr_register resets to 0x24.
    selected_   = 0x24;

    // G62: VHDL zxnext.vhd:1102 declares
    //   signal nr_03_config_mode : std_logic := '1';
    // The only mutator is the NR 0x03 write_handler at :5147-5151 (set on
    // bits[2:0]=111, clear on bits[2:0] ∈ {001..110}). NO explicit reset
    // clause anywhere in zxnext.vhd, so the latch survives both hard and
    // soft reset — only the signal initialiser at :1102 (FPGA power-on)
    // sets it to '1'. The C++ member initialiser in nextreg.h handles
    // power-on; reset() must NOT overwrite it. Same shape as G63
    // (nr_03_machine_type) — see CFG-07 unit row and CFG-08-INT integration row.
    // nr_03_config_mode_ NOT reset here — VHDL latch survives reset.

    // VHDL zxnext.vhd:1104 — nr_04_romram_bank defaults 0x00 at power-on.
    nr_04_romram_bank_ = 0;

    // VHDL zxnext.vhd:1099-1103 — power-on defaults for the NR 0x03 state:
    //   nr_03_machine_timing = "011" (+3 timing)
    //   nr_03_user_dt_lock   = '0'
    //   nr_03_machine_type   = "011" (+3 machine type)
    // These survive the selected-register shuffle and are composed into the
    // NR 0x03 read at zxnext.vhd:5894.
    //
    // G63: nr_03_machine_type is only mutated by NR 0x03 writes (VHDL
    // zxnext.vhd:5137-5145, gated on config_mode='1'). It has NO explicit
    // reset clause anywhere in zxnext.vhd, so the latch survives both hard
    // and soft reset — only the signal initialiser at :1103 (FPGA
    // power-on) sets it to "011". The C++ member initialiser in
    // nextreg.h handles power-on; reset() must NOT overwrite it.
    // The analogous machine_timing / user_dt_lock latches are also
    // preserved by VHDL but not yet covered by a test row; left at the
    // current reset-clobber behavior pending a dedicated test plan
    // (matches CLAUDE.md's "scope to the task" rule).
    nr_03_machine_timing_ = 0x03;
    nr_03_user_dt_lock_   = false;
    // nr_03_machine_type_ NOT reset here — VHDL latch survives reset.
}

void NextReg::apply_nr_03_config_mode_transition(uint8_t low3) {
    // VHDL zxnext.vhd:5147-5151 state machine on NR 0x03 bits[2:0]:
    //   111           → set   (re-enter config_mode)
    //   000           → no change
    //   001..110 else → clear (exit config_mode; machine_type commit happens
    //                          in the emulator-tier handler, gated by the
    //                          CURRENT config_mode at write time per line 5137)
    const uint8_t t = low3 & 0x07;
    if (t == 0x07) {
        if (!nr_03_config_mode_) Log::nextreg()->debug("NR 0x03: config_mode ← 1 (write bits 111)");
        nr_03_config_mode_ = true;
    } else if (t != 0x00) {
        if (nr_03_config_mode_) Log::nextreg()->debug("NR 0x03: config_mode ← 0 (write bits {:03b})", t);
        nr_03_config_mode_ = false;
    }
    // t == 0x00: no change
}

void NextReg::select(uint8_t reg) { selected_ = reg; }

uint8_t NextReg::read_selected() { return read(selected_); }

void NextReg::write_selected(uint8_t val) { write(selected_, val); }

uint8_t NextReg::read(uint8_t reg) {
    uint8_t val;
    if (read_handlers_[reg]) {
        val = read_handlers_[reg]();
    } else {
        val = regs_[reg];
    }
    Log::nextreg()->trace("NextREG read  reg={:#04x} val={:#04x}", reg, val);
    return val;
}

void NextReg::write(uint8_t reg, uint8_t val) {
    Log::nextreg()->trace("NextREG write reg={:#04x} val={:#04x}", reg, val);
    // G56 closure (Option C strategy b1): if a handler is registered, it
    // returns the canonical byte to be stored in regs_[reg]. Without a
    // handler, fall through to the raw written byte. Pre-G56 the raw byte
    // was always stored before calling the handler, which silently
    // overwrote any handler decision to mask reserved bits or reject
    // gated writes — making `regs_[]` diverge from the VHDL-faithful
    // value the handler had committed to subsystem state.
    if (write_handlers_[reg]) {
        regs_[reg] = write_handlers_[reg](val);
    } else {
        regs_[reg] = val;
    }
}

// G88: NMI return-address shadow latch.
// VHDL zxnext.vhd:2050-2085, 6232-6236 — at the NMIACK_LSB cycle the CPU pushes
// PCL to the stack and the same byte is latched into nr_c2_retn_address_lsb;
// at NMIACK_MSB the same happens for PCH → nr_c3_retn_address_msb. Both regs
// are read-only via the 0x243B/0x253B path (no software write handler), so we
// write directly into regs_[] rather than going through NextReg::write().
void NextReg::set_nmi_return_address(uint16_t pc) {
    regs_[0xC2] = static_cast<uint8_t>(pc & 0xFF);
    regs_[0xC3] = static_cast<uint8_t>((pc >> 8) & 0xFF);
}

void NextReg::set_write_handler(uint8_t reg, std::function<uint8_t(uint8_t)> fn) {
    write_handlers_[reg] = std::move(fn);
}

void NextReg::set_read_handler(uint8_t reg, std::function<uint8_t()> fn) {
    read_handlers_[reg] = std::move(fn);
}

void NextReg::save_state(StateWriter& w) const
{
    w.write_u8(selected_);
    w.write_bytes(regs_.data(), 256);
    // nr_03_config_mode_ appended at the end. Feeds the in-process rewind
    // ring buffer only, so snapshot format compatibility across builds is
    // not a concern here.
    w.write_bool(nr_03_config_mode_);
    w.write_u8(nr_04_romram_bank_);
    // NR 0x03 composed-read state (VHDL zxnext.vhd:1099-1103, :5894).
    w.write_u8(nr_03_machine_timing_);
    w.write_bool(nr_03_user_dt_lock_);
    w.write_u8(nr_03_machine_type_);
}

void NextReg::load_state(StateReader& r)
{
    selected_ = r.read_u8();
    r.read_bytes(regs_.data(), 256);
    nr_03_config_mode_ = r.read_bool();
    nr_04_romram_bank_ = r.read_u8();
    nr_03_machine_timing_ = r.read_u8() & 0x07;
    nr_03_user_dt_lock_   = r.read_bool();
    nr_03_machine_type_   = r.read_u8() & 0x07;
}
