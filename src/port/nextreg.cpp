#include "nextreg.h"
#include "core/log.h"

NextReg::NextReg() { reset(); }

void NextReg::reset() {
    regs_.fill(0);
    // Reset defaults from VHDL / ZX Next documentation
    regs_[0x00] = 0x08;  // machine ID: Emulators (HWID_EMULATORS from TBBLUE firmware)
    regs_[0x01] = 0x32;  // core version 3.02 (VHDL g_version = X"32")
    regs_[0x07] = 0x00;  // CPU speed: 3.5 MHz
    regs_[0x03] = 0x00;  // machine type: ZXNext
    selected_   = 0;
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
