#include "z80n_ext.h"
#include "z80_cpu.h"

int execute_z80n(uint8_t opcode, Z80Cpu& cpu) {
    switch (static_cast<Z80NOpcode>(opcode)) {

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

        case Z80NOpcode::LDIX: {
            // ED A4 — single iteration block transfer with transparency
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            uint8_t temp = cpu.memory().read(regs.HL);
            if (temp != A) {
                cpu.memory().write(regs.DE, temp);
            }
            regs.DE = (regs.DE + 1) & 0xFFFF;
            regs.HL = (regs.HL + 1) & 0xFFFF;
            regs.BC = (regs.BC - 1) & 0xFFFF;
            cpu.set_registers(regs);
            return 13;
        }

        case Z80NOpcode::LDDX: {
            // ED AC — single iteration, HL decrements
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            uint8_t temp = cpu.memory().read(regs.HL);
            if (temp != A) {
                cpu.memory().write(regs.DE, temp);
            }
            regs.DE = (regs.DE + 1) & 0xFFFF;  // DE still increments
            regs.HL = (regs.HL - 1) & 0xFFFF;  // HL decrements
            regs.BC = (regs.BC - 1) & 0xFFFF;
            cpu.set_registers(regs);
            return 13;
        }

        case Z80NOpcode::LDIRX: {
            // ED B4 — repeating block transfer with transparency, HL/DE increment
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            // BC=0 on entry means 65536 iterations
            uint32_t count = (regs.BC == 0) ? 65536 : regs.BC;
            for (uint32_t i = 0; i < count; ++i) {
                uint8_t temp = cpu.memory().read(regs.HL);
                if (temp != A) {
                    cpu.memory().write(regs.DE, temp);
                }
                regs.DE = (regs.DE + 1) & 0xFFFF;
                regs.HL = (regs.HL + 1) & 0xFFFF;
            }
            regs.BC = 0;
            cpu.set_registers(regs);
            return 13 * count;
        }

        case Z80NOpcode::LDDRX: {
            // ED BC — repeating block transfer with transparency, HL/DE decrement
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            uint32_t count = (regs.BC == 0) ? 65536 : regs.BC;
            for (uint32_t i = 0; i < count; ++i) {
                uint8_t temp = cpu.memory().read(regs.HL);
                if (temp != A) {
                    cpu.memory().write(regs.DE, temp);
                }
                regs.DE = (regs.DE - 1) & 0xFFFF;
                regs.HL = (regs.HL - 1) & 0xFFFF;
            }
            regs.BC = 0;
            cpu.set_registers(regs);
            return 13 * count;
        }

        case Z80NOpcode::LDPIRX: {
            // ED B7 — pattern fill with transparency, repeats until BC=0
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            uint32_t count = (regs.BC == 0) ? 65536 : regs.BC;
            for (uint32_t i = 0; i < count; ++i) {
                // Source: upper 13 bits of HL | lower 3 bits of E
                uint16_t src_addr = (regs.HL & 0xFFF8) | (regs.DE & 0x0007);
                uint8_t temp = cpu.memory().read(src_addr);
                if (temp != A) {
                    cpu.memory().write(regs.DE, temp);
                }
                regs.DE = (regs.DE + 1) & 0xFFFF;
                // HL does NOT change (pattern base)
            }
            regs.BC = 0;
            cpu.set_registers(regs);
            return 13 * count;
        }

        case Z80NOpcode::LDIRSCALE: {
            // ED B6 — scaled block load, HL increments by BC' each iteration
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            uint16_t bc_alt = regs.BC2;  // alternate BC holds HL increment
            uint32_t count = (regs.BC == 0) ? 65536 : regs.BC;
            for (uint32_t i = 0; i < count; ++i) {
                uint8_t temp = cpu.memory().read(regs.HL);
                if (temp != A) {
                    cpu.memory().write(regs.DE, temp);
                }
                regs.DE = (regs.DE + 1) & 0xFFFF;
                regs.HL = (regs.HL + bc_alt) & 0xFFFF;
            }
            regs.BC = 0;
            cpu.set_registers(regs);
            return 13 * count;
        }

        // TODO: implement remaining Z80N opcodes (currently stubs)
        case Z80NOpcode::SWAPNIB:    return 8;
        case Z80NOpcode::MIRROR_A:   return 8;
        case Z80NOpcode::TEST_N:     return 11;
        case Z80NOpcode::BSLA_DE_B:  return 8;
        case Z80NOpcode::BSRA_DE_B:  return 8;
        case Z80NOpcode::BSRL_DE_B:  return 8;
        case Z80NOpcode::BSRF_DE_B:  return 8;
        case Z80NOpcode::BRLC_DE_B:  return 8;
        case Z80NOpcode::MUL_DE:     return 8;
        case Z80NOpcode::ADD_HL_A:   return 8;
        case Z80NOpcode::ADD_DE_A:   return 8;
        case Z80NOpcode::ADD_BC_A:   return 8;
        case Z80NOpcode::NEXTREG_NN: return 20;
        case Z80NOpcode::NEXTREG_A:  return 17;
        case Z80NOpcode::PIXELDN:    return 8;
        case Z80NOpcode::PIXELAD:    return 8;
        case Z80NOpcode::SETAE:      return 8;
        case Z80NOpcode::OUTINB:     return 16;
        case Z80NOpcode::LOOP:       return 13;
        default: return -1;
    }
}
