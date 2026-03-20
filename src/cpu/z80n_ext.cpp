#include "z80n_ext.h"
#include "z80_cpu.h"

// Flag bit positions for TEST_N
static constexpr uint8_t FLAG_C = 0x01;
static constexpr uint8_t FLAG_N = 0x02;
static constexpr uint8_t FLAG_P = 0x04;
static constexpr uint8_t FLAG_X = 0x08;  // bit 3 (undocumented)
static constexpr uint8_t FLAG_H = 0x10;
static constexpr uint8_t FLAG_Y = 0x20;  // bit 5 (undocumented)
static constexpr uint8_t FLAG_Z = 0x40;
static constexpr uint8_t FLAG_S = 0x80;

// Even parity: returns true when number of 1-bits is even
static inline bool parity_even(uint8_t v) {
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (v & 1) == 0;
}

int execute_z80n(uint8_t opcode, Z80Cpu& cpu) {
    switch (static_cast<Z80NOpcode>(opcode)) {

        case Z80NOpcode::SWAPNIB: {
            auto regs = cpu.get_registers();
            uint8_t a = regs.AF >> 8;
            a = ((a & 0x0F) << 4) | ((a >> 4) & 0x0F);
            regs.AF = ((uint16_t)a << 8) | (regs.AF & 0xFF);
            cpu.set_registers(regs);
            return 4;
        }

        case Z80NOpcode::MIRROR_A: {
            auto regs = cpu.get_registers();
            uint8_t a = regs.AF >> 8;
            uint8_t r = 0;
            for (int i = 0; i < 8; i++) {
                r = (r << 1) | (a & 1);
                a >>= 1;
            }
            regs.AF = ((uint16_t)r << 8) | (regs.AF & 0xFF);
            cpu.set_registers(regs);
            return 4;
        }

        case Z80NOpcode::TEST_N: {
            auto regs = cpu.get_registers();
            uint8_t nn = cpu.memory().read(regs.PC);
            regs.PC = (regs.PC + 1) & 0xFFFF;
            uint8_t a = regs.AF >> 8;
            uint8_t temp = a & nn;
            uint8_t f = FLAG_H;  // H always set
            if (temp & 0x80) f |= FLAG_S;
            if (temp == 0)   f |= FLAG_Z;
            if (parity_even(temp)) f |= FLAG_P;
            if (temp & 0x08) f |= FLAG_X;  // undocumented bit 3
            if (temp & 0x20) f |= FLAG_Y;  // undocumented bit 5
            // N=0, C=0 (already 0)
            regs.AF = ((uint16_t)a << 8) | f;
            cpu.set_registers(regs);
            return 7;
        }

        case Z80NOpcode::BSLA_DE_B: {
            auto regs = cpu.get_registers();
            uint8_t shift = (regs.BC >> 8) & 0x1F;
            regs.DE = (regs.DE << shift) & 0xFFFF;
            cpu.set_registers(regs);
            return 4;
        }

        case Z80NOpcode::BSRA_DE_B: {
            auto regs = cpu.get_registers();
            uint8_t shift = (regs.BC >> 8) & 0x1F;
            int16_t de_signed = static_cast<int16_t>(regs.DE);
            regs.DE = static_cast<uint16_t>(de_signed >> shift);
            cpu.set_registers(regs);
            return 4;
        }

        case Z80NOpcode::BSRL_DE_B: {
            auto regs = cpu.get_registers();
            uint8_t shift = (regs.BC >> 8) & 0x1F;
            regs.DE = regs.DE >> shift;
            cpu.set_registers(regs);
            return 4;
        }

        case Z80NOpcode::BSRF_DE_B: {
            auto regs = cpu.get_registers();
            uint8_t shift = (regs.BC >> 8) & 0x1F;
            if (shift == 0) {
                // No change
            } else {
                uint16_t mask = static_cast<uint16_t>(0xFFFF << (16 - shift));
                regs.DE = (regs.DE >> shift) | mask;
            }
            cpu.set_registers(regs);
            return 4;
        }

        case Z80NOpcode::BRLC_DE_B: {
            auto regs = cpu.get_registers();
            uint8_t rot = (regs.BC >> 8) & 0x0F;
            if (rot != 0) {
                regs.DE = ((regs.DE << rot) | (regs.DE >> (16 - rot))) & 0xFFFF;
            }
            cpu.set_registers(regs);
            return 4;
        }

        case Z80NOpcode::MUL_DE: {
            auto regs = cpu.get_registers();
            uint8_t d = regs.DE >> 8;
            uint8_t e = regs.DE & 0xFF;
            regs.DE = (uint16_t)d * (uint16_t)e;
            cpu.set_registers(regs);
            return 4;
        }

        case Z80NOpcode::ADD_HL_A: {
            auto regs = cpu.get_registers();
            uint8_t a = regs.AF >> 8;
            uint32_t result = (uint32_t)regs.HL + (uint32_t)a;
            regs.HL = result & 0xFFFF;
            // Only C flag changes; preserve all other flags
            uint8_t f = regs.AF & 0xFF;
            f = (f & ~FLAG_C) | ((result >> 16) & 1);
            regs.AF = ((uint16_t)a << 8) | f;
            cpu.set_registers(regs);
            return 4;
        }

        case Z80NOpcode::ADD_DE_A: {
            auto regs = cpu.get_registers();
            uint8_t a = regs.AF >> 8;
            uint32_t result = (uint32_t)regs.DE + (uint32_t)a;
            regs.DE = result & 0xFFFF;
            uint8_t f = regs.AF & 0xFF;
            f = (f & ~FLAG_C) | ((result >> 16) & 1);
            regs.AF = ((uint16_t)a << 8) | f;
            cpu.set_registers(regs);
            return 4;
        }

        case Z80NOpcode::ADD_BC_A: {
            auto regs = cpu.get_registers();
            uint8_t a = regs.AF >> 8;
            uint32_t result = (uint32_t)regs.BC + (uint32_t)a;
            regs.BC = result & 0xFFFF;
            uint8_t f = regs.AF & 0xFF;
            f = (f & ~FLAG_C) | ((result >> 16) & 1);
            regs.AF = ((uint16_t)a << 8) | f;
            cpu.set_registers(regs);
            return 4;
        }

        case Z80NOpcode::PUSH_NN: {
            // ED 8A hh ll — push 16-bit immediate value onto stack.
            // Instruction stream: high byte first, then low byte (big-endian).
            // PC already points past ED 8A; read 2 operand bytes.
            auto regs = cpu.get_registers();
            uint8_t hh = cpu.memory().read(regs.PC);
            uint8_t ll = cpu.memory().read(regs.PC + 1);
            regs.PC = (regs.PC + 2) & 0xFFFF;
            regs.SP = (regs.SP - 2) & 0xFFFF;
            cpu.memory().write(regs.SP + 1, hh);
            cpu.memory().write(regs.SP, ll);
            cpu.set_registers(regs);
            return 23;
        }

        case Z80NOpcode::OUTINB: {
            auto regs = cpu.get_registers();
            uint8_t temp = cpu.memory().read(regs.HL);
            uint16_t port = regs.BC;
            cpu.io().out(port, temp);
            regs.HL = (regs.HL + 1) & 0xFFFF;
            // B is NOT decremented (unlike OUTI)
            cpu.set_registers(regs);
            return 10;
        }

        case Z80NOpcode::NEXTREG_NN: {
            auto regs = cpu.get_registers();
            uint8_t reg = cpu.memory().read(regs.PC);
            uint8_t val = cpu.memory().read((regs.PC + 1) & 0xFFFF);
            regs.PC = (regs.PC + 2) & 0xFFFF;
            cpu.set_registers(regs);
            cpu.io().out(0x243B, reg);
            cpu.io().out(0x253B, val);
            return 16;
        }

        case Z80NOpcode::NEXTREG_A: {
            auto regs = cpu.get_registers();
            uint8_t reg = cpu.memory().read(regs.PC);
            regs.PC = (regs.PC + 1) & 0xFFFF;
            uint8_t a = regs.AF >> 8;
            cpu.set_registers(regs);
            cpu.io().out(0x243B, reg);
            cpu.io().out(0x253B, a);
            return 13;
        }

        case Z80NOpcode::PIXELDN: {
            auto regs = cpu.get_registers();
            uint8_t H = regs.HL >> 8;
            uint8_t L = regs.HL & 0xFF;
            // ULA screen layout: H = 010BBLLL, L = CCCCCRRR
            // PIXELDN increments the row counter encoded across H and L
            uint8_t inner = ((H & 0x07) + 1);
            if ((inner & 0x08) == 0) {
                H = (H & 0xF8) | (inner & 0x07);
            } else {
                H = H & 0xF8;
                uint8_t mid = ((L >> 5) + 1) & 0x07;
                L = (L & 0x1F) | (mid << 5);
                if (mid == 0) {
                    H = H + 0x08;
                }
            }
            regs.HL = ((uint16_t)H << 8) | L;
            cpu.set_registers(regs);
            return 4;
        }

        case Z80NOpcode::PIXELAD: {
            auto regs = cpu.get_registers();
            uint8_t D = regs.DE >> 8;    // X coord
            uint8_t E = regs.DE & 0xFF;  // Y coord
            uint8_t H = 0x40 | ((E & 0xC0) >> 3) | (E & 0x07);
            uint8_t L = ((E & 0x38) << 2) | (D >> 3);
            regs.HL = ((uint16_t)H << 8) | L;
            cpu.set_registers(regs);
            return 4;
        }

        case Z80NOpcode::SETAE: {
            auto regs = cpu.get_registers();
            uint8_t e = regs.DE & 0xFF;
            uint8_t bit = e & 0x07;
            uint8_t a = 0x80 >> bit;
            regs.AF = ((uint16_t)a << 8) | (regs.AF & 0xFF);
            cpu.set_registers(regs);
            return 4;
        }

        // Block transfer opcodes — stubs, implemented by another agent
        case Z80NOpcode::LDIX:       return 13;
        case Z80NOpcode::LDDX:       return 13;
        case Z80NOpcode::LDIRX:      return 13;
        case Z80NOpcode::LDIRSCALE:  return 13;
        case Z80NOpcode::LDPIRX:     return 13;
        case Z80NOpcode::LDDRX:      return 13;

        case Z80NOpcode::LOOP: {
            // Not implemented in FPGA. Treat as NOP.
            return 4;
        }

        default: return -1;
    }
}
