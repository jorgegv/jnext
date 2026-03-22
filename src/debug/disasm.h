#pragma once
#include <cstdint>
#include <functional>
#include <string>

struct DisasmLine {
    uint16_t addr;           // address of instruction
    uint8_t  bytes[4];       // raw bytes (max 4 for standard Z80; DDCB/FDCB can be 4)
    int      byte_count;     // 1-4
    char     mnemonic[64];   // null-terminated mnemonic string
};

/// Read callback: reads one byte from the given address.
using DisasmReadFn = std::function<uint8_t(uint16_t)>;

/// Disassemble one instruction at `addr`.
/// Returns the DisasmLine. `addr` is NOT modified (caller advances by byte_count).
DisasmLine disasm_one(uint16_t addr, DisasmReadFn read_fn);

/// Get instruction length at addr without full disassembly (faster).
int instruction_length(uint16_t addr, DisasmReadFn read_fn);

/// Returns true if the instruction at addr is CALL-like (CALL nn, CALL cc,nn, RST n, DJNZ).
/// Used by step-over to decide whether to set a one-shot breakpoint.
bool is_call_like(uint16_t addr, DisasmReadFn read_fn);

/// Returns true if the instruction at addr is a RET-like (RET, RET cc, RETI, RETN).
/// Used by step-out.
bool is_ret_like(uint16_t addr, DisasmReadFn read_fn);
