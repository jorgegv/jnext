#pragma once
#include <cstdint>
#include <functional>

struct Z80Registers {
    uint16_t AF, BC, DE, HL;
    uint16_t AF2, BC2, DE2, HL2;
    uint16_t IX, IY, SP, PC;
    uint8_t  I, R;
    uint8_t  IFF1, IFF2, IM;
    bool halted;
};

class MemoryInterface {
public:
    virtual ~MemoryInterface() = default;
    virtual uint8_t read(uint16_t addr) = 0;
    virtual void write(uint16_t addr, uint8_t val) = 0;
};

class IoInterface {
public:
    virtual ~IoInterface() = default;
    virtual uint8_t in(uint16_t port) = 0;
    virtual void out(uint16_t port, uint8_t val) = 0;
};

// Z80 CPU wrapper stub — pending libz80 / FUSE z80 integration
// TODO: integrate libz80 (https://github.com/anotherlin/z80emu) or FUSE z80 core
class Z80Cpu {
public:
    Z80Cpu(MemoryInterface& mem, IoInterface& io);

    void reset();
    int  execute();   // execute one instruction; returns T-states used

    Z80Registers get_registers() const { return regs_; }
    void set_registers(const Z80Registers& r) { regs_ = r; }

    void request_interrupt(uint8_t vector);
    void request_nmi();
    bool is_halted() const { return regs_.halted; }

    // Callback fired on M1 cycle — used for RETI detection and IM2
    std::function<void(uint16_t pc, uint8_t opcode)> on_m1_cycle;

private:
    MemoryInterface& mem_;
    IoInterface&     io_;
    Z80Registers     regs_{};
    bool             nmi_pending_ = false;
    bool             int_pending_ = false;
    uint8_t          int_vector_  = 0xFF;
};
