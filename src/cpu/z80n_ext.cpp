#include "z80n_ext.h"
#include "z80_cpu.h"

int execute_z80n(uint8_t opcode, Z80Cpu& cpu) {
    // TODO: implement each Z80N opcode
    switch (static_cast<Z80NOpcode>(opcode)) {
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
