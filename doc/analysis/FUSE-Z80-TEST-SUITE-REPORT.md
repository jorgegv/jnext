# FUSE Z80 Opcode Test Suite Report

**Date:** 2026-04-12
**Result:** 1356 / 1356 pass (100%)

---

## Overview

The FUSE Z80 opcode test suite is the standard conformance test for Z80
emulators, maintained as part of the [FUSE emulator](https://fuse-emulator.sourceforge.net/)
project. It tests every documented Z80 instruction with specific initial
register/memory state and verifies the final state after execution.

The test data consists of two files:
- `tests.in` (9153 lines) — 1356 test cases with initial CPU and memory state
- `tests.expected` (18913 lines) — expected final state including bus events

Our test runner (`test/fuse_z80_test.cpp`) parses both files, executes each
test in isolation using a 64K RAM mock and null I/O, then compares registers
and memory against expected values.

---

## Results Summary

| Category | Tests | Pass | Fail |
|----------|-------|------|------|
| Standard Z80 opcodes | 1356 | 1356 | 0 |
| **Pass rate** | | | **100%** |

---

## History of Fixes

### BIT n,(HL) — Undocumented YF flag (12 tests, fixed)

**Tests:** `cb46`, `cb46_1`–`cb46_5`, `cb4e`, `cb56`, `cb5e`, `cb66`,
`cb6e`, `cb76`, `cb7e`

Flag bit 5 (YF) for `BIT n,(HL)` depends on the internal MEMPTR register.
Fixed by syncing MEMPTR between the FUSE core and the test runner's register
state.

### SCF — Undocumented flags 3 and 5 (1 test, fixed)

**Test:** `37`

`SCF` sets undocumented flag bits based on the internal Q register.
Fixed by syncing the Q register between the FUSE core and the test runner.

### DD/FD Prefix Chains (2 tests, fixed)

**Tests:** `dd00`, `ddfd00`

These tests contain sequences where DD/FD prefixes are followed by NOP,
effectively creating multi-instruction test cases. Fixed by adding a
multi-instruction runner that loops `execute()` until the expected PC is
reached.

### DJNZ Loop (1 test, fixed)

**Test:** `10`

This test encodes a DJNZ loop (B=8) that requires multiple execute() calls.
Fixed by the same multi-instruction runner used for DD/FD prefix tests.

---

## Test Infrastructure

### Test Runner

`test/fuse_z80_test.cpp` — standalone C++ executable (no GoogleTest required).

**Features:**
- Parses FUSE `tests.in` / `tests.expected` text format
- Creates isolated 64K RAM + null I/O per test
- Port reads return high byte of port address (floating bus behavior)
- Handles repeating block instructions (LDIR, LDDR, CPIR, CPDR, INIR, INDR,
  OTIR, OTDR) by looping until completion
- Handles multi-instruction tests (10, dd00, ddfd00) with a separate runner
  that loops execute() until the expected PC is reached
- Syncs MEMPTR and Q registers for full undocumented flag accuracy
- Compares all registers (AF, BC, DE, HL, AF', BC', DE', HL', IX, IY, SP, PC,
  I, R, IFF1, IFF2, IM, halted) and expected memory contents

### Building and Running

```bash
cmake --build build -j$(nproc)
./build/test/fuse_z80_test build/test/fuse
```

Or via CTest:

```bash
cd build && ctest -R fuse_z80
```

### Test Data

The FUSE test data is stored in `test/fuse/` and originates from the FUSE
emulator project (GPLv2). The files are copied to the build directory during
CMake configuration.

---

## What the Tests Cover

The 1356 tests cover:

- **All documented Z80 opcodes** (00-FF base, CB xx, ED xx, DD/FD xx, DD/FD CB xx)
- **Flag behavior** for arithmetic, logic, shift, rotate, and compare instructions
- **Memory access patterns** (LD, PUSH, POP, EX, block transfers)
- **Conditional branches** (JR, JP, CALL, RET with all conditions)
- **Interrupt control** (EI, DI, IM 0/1/2)
- **Block instructions** (LDI, LDIR, LDD, LDDR, CPI, CPIR, CPD, CPDR)
- **Block I/O** (INI, INIR, IND, INDR, OUTI, OTIR, OUTD, OTDR)
- **Bit manipulation** (BIT, SET, RES for all bit positions)
- **Arithmetic** (ADD, ADC, SUB, SBC, AND, OR, XOR, CP, INC, DEC, DAA, NEG)
- **16-bit arithmetic** (ADD HL, ADC HL, SBC HL)
- **Rotate/shift** (RLCA, RRCA, RLA, RRA, RLC, RRC, RL, RR, SLA, SRA, SRL, SLL)
- **Exchange** (EX AF, EXX, EX DE,HL, EX (SP),HL/IX/IY)
- **Stack** (PUSH, POP for all register pairs)
- **I/O** (IN A,(n), IN r,(C), OUT (n),A, OUT (C),r)
- **Miscellaneous** (NOP, HALT, SCF, CCF, CPL, NEG, RETI, RETN, RST)

### What the Tests Do NOT Cover

- Z80N extension opcodes (SWAPNIB, MIRROR, LDIX, etc.) — these are Next-specific
- Cycle-accurate bus timing (we compare registers/memory, not MR/MW/PR/PW events)
- Interrupt response timing
- Contention effects
