#include "nextreg.h"
#include "core/log.h"
#include "core/saveable.h"

NextReg::NextReg() { reset(); }

void NextReg::reset() {
    // VHDL zxnext.vhd:5052-5057: on soft reset, NR 0x82-0x84 reload to
    // 0xFF only when reset_type (NR 0x85 bit 7) is 1. When reset_type=0,
    // the previous values are preserved across the reset.
    const bool reset_type_1 = (regs_[0x85] & 0x80) != 0;
    const uint8_t saved_82 = regs_[0x82];
    const uint8_t saved_83 = regs_[0x83];
    const uint8_t saved_84 = regs_[0x84];

    regs_.fill(0);
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
    regs_[0x05] = 0x40;  // joy config: VHDL zxnext.vhd:1105-1106 (nr_05 not cleared on soft reset)
    regs_[0x07] = 0x00;  // CPU speed: 3.5 MHz
    regs_[0x0B] = 0x01;  // IO mode: VHDL zxnext.vhd:4939-4941 (iomode_0=1 on reset)
    // Sub-version: VHDL g_sub_version = X"03" generic in
    // zxnext_top_issue2.vhd:38 (also issue4/issue5). Read mux at
    // zxnext.vhd:5917-5918 returns g_sub_version verbatim.
    regs_[0x0E] = 0x03;
    // Port-enable registers: VHDL zxnext.vhd:1226-1230, 5052-5057.
    if (reset_type_1) {
        regs_[0x82] = 0xFF;
        regs_[0x83] = 0xFF;
        regs_[0x84] = 0xFF;
    } else {
        regs_[0x82] = saved_82;
        regs_[0x83] = saved_83;
        regs_[0x84] = saved_84;
    }
    regs_[0x85] = 0x8F;  // bit7=reset_type(1), bits6:4=0, bits3:0=0xF (enables)
    // NR 0x89 bus port enables: VHDL zxnext.vhd:1234-1235 —
    // nr_89_bus_port_reset_type='1' and nr_89_bus_port_enable=(others=>'1').
    // Read mux at zxnext.vhd:6147-6150 composes
    //   nr_89_bus_port_reset_type & "000" & nr_89_bus_port_enable
    // → 0x8F (bit7=1, bits6:4=0, bits3:0=0xF).
    regs_[0x89] = 0x8F;
    // VHDL zxnext.vhd:4594-4596 — nr_register resets to 0x24.
    selected_   = 0x24;

    // VHDL zxnext.vhd:1102 — nr_03_config_mode defaults '1' at power-on (signal
    // initialiser, re-asserted on hard reset). Soft resets don't clear it
    // either, as there is no code path that resets the signal other than via
    // NR 0x03 writes; safest and VHDL-faithful is to re-default on reset().
    nr_03_config_mode_ = true;

    // VHDL zxnext.vhd:1104 — nr_04_romram_bank defaults 0x00 at power-on.
    nr_04_romram_bank_ = 0;
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
    regs_[reg] = val;
    if (write_handlers_[reg]) {
        write_handlers_[reg](val);
    }
}

void NextReg::set_write_handler(uint8_t reg, std::function<void(uint8_t)> fn) {
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
}

void NextReg::load_state(StateReader& r)
{
    selected_ = r.read_u8();
    r.read_bytes(regs_.data(), 256);
    nr_03_config_mode_ = r.read_bool();
    nr_04_romram_bank_ = r.read_u8();
}
