#pragma once
// GH #367 - what the instruction at the PC is about to do: whether a conditional branch is taken,
// and the value at the one memory operand its mnemonic names. Qt-free, machine semantics, so any
// frontend can use it (the Qt Disassembly panel does). A sibling of disasm_text.h, not part of the
// four frozen facade headers.
//
// The oracle is Zilog UM0080 (opcode and condition-code tables). The decode mirrors disasm.cpp's
// treatment of DD/FD prefixes, so the hint always describes the line the panel shows; the suite pins
// the two together over the whole opcode space.
#include "cpu/z80_cpu.h"
#include "debug/disasm.h"
#include "debug/disasm_text.h"

#include <cstdint>
#include <string>

namespace disasm_hint {

struct Hint {
    enum class Kind { None, Branch, Memory };
    Kind     kind   = Kind::None;
    // Branch: a conditional JP cc / JR cc / CALL cc / RET cc, or DJNZ.
    bool     taken  = false;
    uint16_t target = 0;          // JR/DJNZ: inner address + 2 + d; JP/CALL: nn; RET: the word at SP
    bool     from_stack = false;  // RET cc: the target is not in the mnemonic, so the hint shows it
    // Memory: the one memory operand the mnemonic names in parentheses.
    uint16_t addr   = 0;
    int      width  = 0;          // 1 or 2
    uint16_t value  = 0;          // the byte, or the little-endian word (the high byte wraps at 64K)
};

/// `code` reads the instruction bytes (pass Debugger::memory_reader(): the bytes the CPU will fetch
/// and the ones the panel shows); `data` reads the operand (pass a peek(MemSpace::cpu()) reader).
/// Neither may disturb the machine; the helper never reads a port, and calls `data` only for the
/// operand it reports (and the RET cc return address).
Hint pc_hint(const Z80Registers& r, const DisasmReadFn& code, const DisasmReadFn& data);

/// "[taken]", "[not taken]", "[taken -> T]" (RET cc only, T named by `namer` or $XXXX),
/// "[(A)=$VV]" / "[(A)=$VVVV]" (A named by `namer` or $XXXX; the value is never named);
/// "" for None.
std::string format_hint(const Hint& h, const disasm_text::SymbolNamer& namer);

} // namespace disasm_hint
