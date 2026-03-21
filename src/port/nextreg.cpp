#include "nextreg.h"
#include "core/log.h"

NextReg::NextReg() { reset(); }

void NextReg::reset() {
    regs_.fill(0);
    // Reset defaults from VHDL / ZX Next documentation
    regs_[0x00] = 0x08;  // machine ID: ZX Spectrum Next
    regs_[0x01] = 0x30;  // core version 3.0
    regs_[0x07] = 0x00;  // CPU speed: 3.5 MHz
    regs_[0x03] = 0x00;  // machine type: ZXNext
    selected_   = 0;
}

void NextReg::select(uint8_t reg) { selected_ = reg; }

uint8_t NextReg::read_selected() { return read(selected_); }

void NextReg::write_selected(uint8_t val) { write(selected_, val); }

uint8_t NextReg::read(uint8_t reg) {
    if (read_handlers_[reg]) {
        return read_handlers_[reg]();
    }
    return regs_[reg];
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
