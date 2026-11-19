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
/// and the ones the panel shows); `data` reads the operand (pass
/// Debugger::operand_reader(); peek(MemSpace::cpu()) differs from it at a DivMMC/Multiface entry point).
/// Neither may disturb the machine; the helper never reads a port, and calls `data` only for the
/// operand it reports (and the RET cc return address).
Hint pc_hint(const Z80Registers& r, const DisasmReadFn& code, const DisasmReadFn& data);

/// What Follow does on an instruction (GH #366): jump the Disassembly to a code address, or show a
/// data address in the Memory panel.
struct Follow {
    enum class Kind { None, Code, Data };
    Kind     kind = Kind::None;
    uint16_t addr = 0;
};

/// What Follow does on the instruction at `at` (not necessarily the PC), from the registers NOW.
/// Code: JP, CALL, JR, DJNZ and RST targets (a conditional's whether or not its condition holds),
/// JP (HL)/(IX)/(IY) (the register), RET/RETI/RETN (the word at SP, read through `data`). Data: the
/// one memory operand the mnemonic names (pc_hint()'s Memory set). None: everything else, JP (C)
/// included (its target needs a port read, which this never does). `code` reads the instruction
/// bytes (Debugger::memory_reader()); `data` the stack word and the operand (pass operand_reader()
/// when `at` is the PC, a peek(MemSpace::cpu()) reader otherwise).
Follow follow_target(const Z80Registers& r, uint16_t at, const DisasmReadFn& code, const DisasmReadFn& data);

/// "[taken]", "[not taken]", "[taken -> T]" (RET cc only, T named by `namer` or $XXXX),
/// "[(A)=$VV]" / "[(A)=$VVVV]" (A named by `namer` or $XXXX; the value is never named);
/// "" for None.
std::string format_hint(const Hint& h, const disasm_text::SymbolNamer& namer);

/// GH #542 - where the instruction at the PC sends the PC, when it transfers control.
struct Transfer {
    enum class Dir { Up, Down, Here };
    bool     taken  = false;      // the PC goes somewhere other than the next instruction (a conditional only when it holds)
    uint16_t target = 0;
    Dir      dir    = Dir::Here;  // target < PC: Up; > PC: Down; == PC: Here (unsigned: the panel's linear order)
    bool     hidden = false;      // the target is written nowhere on the PC line: RET/RETI/RETN (stack), JP (HL)/(IX)/(IY)
                                  // (register). False for RET cc, whose "[taken -> T]" hint names it.
};

/// `code`/`data` exactly as for pc_hint(). Never reads a port; JP (C), HALT and the repeating block
/// instructions are not transfers.
Transfer pc_transfer(const Z80Registers& r, const DisasmReadFn& code, const DisasmReadFn& data);

/// "[-> T]" when t.taken && t.hidden (T named by `namer` or $XXXX), else "".
std::string format_transfer(const Transfer& t, const disasm_text::SymbolNamer& namer);

} // namespace disasm_hint
