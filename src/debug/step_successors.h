#pragma once
// Where the CPU can be after ONE instruction: the addresses a debugger drops temporary
// breakpoints on to step a machine it cannot single-step (a remote target, over DZRP's
// CMD_CONTINUE). Pure: no machine, no port read. Built on the existing decoders
// (instruction_length / is_call_like in disasm.h, pc_hint / follow_target in disasm_hint.h),
// and checked against jnext's own Z80Cpu over every opcode (step_successors_test).
//
// The local Step Over takes its targets from here too, so a local and a remote Step Over can
// never disagree.
#include "cpu/z80_cpu.h"
#include "debug/disasm.h"

#include <array>
#include <cstdint>
#include <functional>

namespace jnext { namespace dbg {

enum class StepKind : uint8_t { Into, Over };

struct StepSuccessors {
    enum class Status : uint8_t { Ok, Unknowable };
    Status   status = Status::Ok;
    uint8_t  count  = 0;                    // 1 or 2 when Ok: distinct addresses
    std::array<uint16_t, 2> addr{};
    const char* why = "";                   // Unknowable: the reason, user-facing
};

/// The addresses the PC can hold after the instruction at r.PC, from the registers NOW.
/// `code` reads instruction bytes (Debugger::memory_reader()), `data` the stack word
/// (Debugger::operand_reader()). `nextreg` reads a NextREG (may be empty: no stackless return);
/// it is read only for a RETN: NR 0xC0, then 0xC2 / 0xC3 only when bit 3 (stackless NMI) is set.
/// Never reads a port.
///
/// Into: a conditional gives both its target and the next instruction (whatever the flags
/// say); a block repeat gives the next instruction and itself; HALT gives the next instruction
/// (the CPU leaves a halt only through an interrupt, whose handler returns there); JP (C) is
/// Unknowable (its target is read from a port). Over: a CALL, RST or DJNZ gives the next
/// instruction (RST $08 also the byte after its esxDOS function byte); anything else gives
/// the Into set. Addresses follow the CPU's own instruction boundaries (a DD/FD chain and its
/// opcode are one instruction, however long); a chain with no end is Unknowable.
StepSuccessors step_successors(const Z80Registers& r, const DisasmReadFn& code, const DisasmReadFn& data,
                               StepKind kind, const std::function<uint8_t(uint8_t)>& nextreg = {});

}} // namespace jnext::dbg
