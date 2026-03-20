#pragma once
#include <cstdint>

class Z80Cpu;

// Z80N extension opcodes (ED-prefix, ZX Spectrum Next specific)
enum class Z80NOpcode : uint8_t {
    SWAPNIB     = 0x23,
    MIRROR_A    = 0x24,
    TEST_N      = 0x27,
    BSLA_DE_B   = 0x28,
    BSRA_DE_B   = 0x29,
    BSRL_DE_B   = 0x2A,
    BSRF_DE_B   = 0x2B,
    BRLC_DE_B   = 0x2C,
    MUL_DE      = 0x30,
    ADD_HL_A    = 0x31,
    ADD_DE_A    = 0x32,
    ADD_BC_A    = 0x33,
    PUSH_NN     = 0x8A,
    OUTINB      = 0x90,
    NEXTREG_NN  = 0x91,
    NEXTREG_A   = 0x92,
    PIXELDN     = 0x93,
    PIXELAD     = 0x94,
    SETAE       = 0x95,
    LDIX        = 0xA4,
    LDDX        = 0xAC,
    LDIRX       = 0xB4,
    LDIRSCALE   = 0xB6,
    LDPIRX      = 0xB7,
    LDDRX       = 0xBC,
    LOOP        = 0xFB,
};

// Returns T-states used, or -1 if opcode is not a Z80N instruction
int execute_z80n(uint8_t opcode, Z80Cpu& cpu);
