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
    regs_[0x00] = 0x0A;  // machine ID: ZX Spectrum Next (VHDL g_machine_id = X"0A")
    regs_[0x01] = 0x32;  // core version 3.02 (VHDL g_version = X"32")
    regs_[0x03] = 0x00;  // machine type: ZXNext
    regs_[0x05] = 0x40;  // joy config: VHDL zxnext.vhd:1105-1106 (nr_05 not cleared on soft reset)
    regs_[0x07] = 0x00;  // CPU speed: 3.5 MHz
    regs_[0x0B] = 0x01;  // IO mode: VHDL zxnext.vhd:4939-4941 (iomode_0=1 on reset)
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
    // VHDL zxnext.vhd:4594-4596 — nr_register resets to 0x24.
    selected_   = 0x24;
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
}

void NextReg::load_state(StateReader& r)
{
    selected_ = r.read_u8();
    r.read_bytes(regs_.data(), 256);
}
