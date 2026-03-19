#include "z80_cpu.h"
#include <cstring>

Z80Cpu::Z80Cpu(MemoryInterface& mem, IoInterface& io)
    : mem_(mem), io_(io) {
    reset();
}

void Z80Cpu::reset() {
    std::memset(&regs_, 0, sizeof(regs_));
    regs_.PC  = 0x0000;
    regs_.SP  = 0xFFFF;
    regs_.AF  = 0xFFFF;
    regs_.IFF1 = 0;
    regs_.IFF2 = 0;
    regs_.IM   = 0;
    regs_.halted = false;
    nmi_pending_ = false;
    int_pending_ = false;
}

int Z80Cpu::execute() {
    // TODO: replace with libz80 or FUSE z80 core call
    uint8_t opcode = mem_.read(regs_.PC);
    if (on_m1_cycle) on_m1_cycle(regs_.PC, opcode);
    ++regs_.PC;
    return 4;
}

void Z80Cpu::request_interrupt(uint8_t vector) {
    int_pending_ = true;
    int_vector_  = vector;
}

void Z80Cpu::request_nmi() {
    nmi_pending_ = true;
}
