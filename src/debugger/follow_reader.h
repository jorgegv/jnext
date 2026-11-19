#pragma once

// GH #366 -- the data reader Follow hands to disasm_hint::follow_target(): the one place the rule is written
// for both the Disassembly and the Source tab. The instruction at the PC reads its operand (and a RET's
// return address) as its own data cycles see the memory map (Debugger::operand_reader(): at a DivMMC or
// Multiface entry point the M1 has already switched the overlay, which peek(Cpu) does not show); any other
// instruction is read as the CPU map is now.

#include "debug/debugger.h"
#include "debug/disasm.h"

inline DisasmReadFn follow_data_reader(const jnext::dbg::Debugger* dbg, bool at_pc)
{
    if (at_pc) return dbg->operand_reader();
    return [dbg](uint16_t a) -> uint8_t {
        uint8_t b = 0;
        dbg->peek(jnext::dbg::MemSpace::cpu(), a, 1, &b);
        return b;
    };
}
