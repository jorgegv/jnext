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
        case Z80NOpcode::LDIX:       return 16;
        case Z80NOpcode::LDDX:       return 16;
        case Z80NOpcode::LDIRX:      return 21;
        case Z80NOpcode::LDIRSCALE:  return 21;
        case Z80NOpcode::LDPIRX:     return 21;
        case Z80NOpcode::LDDRX:      return 21;
        case Z80NOpcode::LOOP:       return 13;
        default: return -1;
    }
}
