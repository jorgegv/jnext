#include "debug/step_successors.h"

#include "debug/disasm_hint.h"

namespace jnext { namespace dbg {

namespace {

StepSuccessors make(std::initializer_list<uint16_t> list) {
    StepSuccessors s;
    for (uint16_t a : list) {
        bool dup = false;
        for (uint8_t i = 0; i < s.count; ++i) dup = dup || s.addr[i] == a;
        if (!dup && s.count < s.addr.size()) s.addr[s.count++] = a;
    }
    return s;
}

StepSuccessors unknowable(const char* why) {
    StepSuccessors s;
    s.status = StepSuccessors::Status::Unknowable;
    s.why = why;
    return s;
}

uint16_t u16(const DisasmReadFn& rd, uint16_t a) {
    return static_cast<uint16_t>(rd(a) | (rd(static_cast<uint16_t>(a + 1)) << 8));
}

// ED opcodes that repeat: the PC goes back to the ED byte until the count runs out.
bool is_block_repeat(uint8_t e) {
    return (e >= 0xB0 && e <= 0xB3) || (e >= 0xB8 && e <= 0xBB)   // LDIR CPIR INIR OTIR, LDDR CPDR INDR OTDR
        || e == 0xB4 || e == 0xB6 || e == 0xB7 || e == 0xBC;      // Z80N LDIRX LDIRSCALE LDPIRX LDDRX
}

} // namespace

StepSuccessors step_successors(const Z80Registers& r, const DisasmReadFn& code, const DisasmReadFn& data,
                               StepKind kind, const std::function<uint8_t(uint8_t)>& nextreg) {
    const uint16_t pc = r.PC;

    if (kind == StepKind::Over && ::is_call_like(pc, code)) {
        // RST $08 is esxDOS: the call returns past its DEFB function byte, so both addresses.
        if (code(pc) == 0xCF)
            return make({static_cast<uint16_t>(pc + 1), static_cast<uint16_t>(pc + 2)});
        return make({static_cast<uint16_t>(pc + ::instruction_length(pc, code))});
    }

    // The DD/FD chain: the CPU runs it and its opcode as one instruction, with no length limit.
    // `a` is the opcode; `e` the LAST prefix (the decoders below see one prefix and the opcode).
    uint16_t a = pc;
    uint32_t prefixes = 0;
    for (;;) {
        const uint8_t b = code(a);
        if (b != 0xDD && b != 0xFD) break;
        if (++prefixes > 0xFFFF) return unknowable("an endless prefix chain");
        a = static_cast<uint16_t>(a + 1);
    }
    const uint16_t e   = prefixes ? static_cast<uint16_t>(a - 1) : pc;
    const uint8_t  op  = code(a);
    const uint8_t  op2 = code(static_cast<uint16_t>(a + 1));

    // The core recognises a Z80N opcode only behind a bare ED (z80_cpu.cpp tests the first byte), so
    // behind a DD/FD prefix every ED opcode is a standard one: 4 bytes for LD (nn),rr / LD rr,(nn), else
    // 2, whatever the disassembler shows for the Z80N form.
    const bool plain_ed = prefixes && op == 0xED;
    const uint16_t fall = plain_ed
        ? static_cast<uint16_t>(a + ((op2 >> 6) == 1 && (op2 & 7) == 3 ? 4 : 2))
        : static_cast<uint16_t>(e + ::instruction_length(e, code));

    if (op == 0x76) return make({fall});                                  // HALT
    if (op == 0xED) {
        if (op2 == 0x98) return unknowable("JP (C) jumps to an address read from a port");
        if (is_block_repeat(op2)) {
            if (plain_ed && (op2 == 0xB4 || op2 == 0xB6 || op2 == 0xB7 || op2 == 0xBC)) return make({fall});
            return make({fall, a});
        }
        if (op2 == 0x4D) return make({u16(data, r.SP)});                  // RETI: always the stack
        if ((op2 & 0xC7) == 0x45) {                                       // RETN and its aliases
            const uint16_t stack = u16(data, r.SP);
            if (nextreg && (nextreg(0xC0) & 0x08)) {
                const uint16_t shadow = static_cast<uint16_t>(nextreg(0xC2) | (nextreg(0xC3) << 8));
                return make({stack, shadow});
            }
            return make({stack});
        }
    }

    Z80Registers at = r;
    at.PC = e;
    const disasm_hint::Hint h = disasm_hint::pc_hint(at, code, data);
    if (h.kind == disasm_hint::Hint::Kind::Branch) return make({fall, h.target});
    const disasm_hint::Follow f = disasm_hint::follow_target(at, e, code, data);
    if (f.kind == disasm_hint::Follow::Kind::Code) return make({f.addr});
    return make({fall});
}

}} // namespace jnext::dbg
