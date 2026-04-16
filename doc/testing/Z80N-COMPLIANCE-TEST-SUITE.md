# Z80N Compliance Test Suite

FUSE-style compliance test suite for the 30 Z80N extended instructions
implemented in the JNEXT emulator. Complements the existing FUSE Z80 test
suite (1356/1356 pass) with dedicated coverage for ZX Spectrum Next-specific
opcodes.

## Purpose

The base Z80 instruction set is validated by the FUSE test suite. The Z80N
extensions (all ED-prefixed) have different I/O requirements, looping
semantics, and flag behaviour that make them unsuitable for the FUSE runner.
This suite provides 85 hand-computed test cases covering all 30 Z80N
instructions, using the same file format and runner pattern as the FUSE tests.

## Architecture

### Separate runner, shared format

The test suite uses the same file-format conventions as FUSE (one test case
per block in `tests.in` / `tests.expected`) but with a dedicated runner
(`z80n_test.cpp`). The runner is separate from `fuse_z80_test.cpp` for three
reasons:

1. **I/O handling** -- NEXTREG (ED 91, ED 92) writes to ports 0x243B/0x253B;
   JP (C) (ED 98) reads from a port. A `Z80NTestIO` class captures OUT calls
   and provides configurable IN return values.
2. **Repeating block instructions** -- LDIRX, LDDRX, LDPIRX, and LDIRSCALE
   execute as internal loops in a single `cpu.execute()` call, unlike the
   FUSE runner which steps one opcode at a time.
3. **Clean separation** -- FUSE tests validate the base Z80 core; Z80N tests
   validate the Next extensions. Neither pollutes the other.

### Test data format

Each test case in `tests.in` specifies:
- Test name (one line)
- Register state: AF, BC, DE, HL, AF', BC', DE', HL', IX, IY, SP, PC
- IFF1, IFF2, IM, halted, tstates
- Memory contents (address followed by byte values, terminated by `-1`)

Each corresponding block in `tests.expected` specifies:
- Test name
- Expected register state after execution
- Expected memory contents
- Events (I/O reads/writes, memory accesses)

PC starts at 0x8000 for all tests. Instruction bytes are placed at 0x8000.
Unused registers carry sentinel values to verify preservation.

## File Layout

```
test/
  z80n/
    tests.in              # Initial state for 85 test cases
    tests.expected        # Expected state after execution
  z80n_test.cpp           # Test runner executable
  CMakeLists.txt          # Updated: new executable + CTest registration
  regression.sh           # Updated: Z80N test phase added
doc/design/
  Z80N-COMPLIANCE-TEST-SUITE.md   # This document
```

## Test Case Catalog

All instructions are ED-prefixed. 85 test cases grouped by
instruction.

| Opcode   | Mnemonic        | Tests | Notes                                    |
|----------|-----------------|------:|------------------------------------------|
| ED 23    | SWAPNIB         |     3 | Swap A nibbles; flags preserved          |
| ED 24    | MIRROR A        |     5 | Bit-reverse A; flags preserved           |
| ED 27 nn | TEST n          |     4 | AND A,n without storing; full flag set   |
| ED 28    | BSLA DE,B       |     4 | Barrel shift left arithmetic             |
| ED 29    | BSRA DE,B       |     4 | Barrel shift right arithmetic (sign ext) |
| ED 2A    | BSRL DE,B       |     3 | Barrel shift right logical               |
| ED 2B    | BSRF DE,B       |     3 | Barrel shift right fill (1-fill)         |
| ED 2C    | BRLC DE,B       |     4 | Barrel rotate left circular              |
| ED 30    | MUL DE          |     4 | D * E -> DE; no flags                    |
| ED 31    | ADD HL,A        |     4 | 16+8 add; updates C flag only            |
| ED 32    | ADD DE,A        |     2 | 16+8 add; updates C flag only            |
| ED 33    | ADD BC,A        |     2 | 16+8 add; updates C flag only            |
| ED 34    | ADD HL,nn       |     3 | 16+16 add; no flags affected             |
| ED 35    | ADD DE,nn       |     1 | 16+16 add; no flags affected             |
| ED 36    | ADD BC,nn       |     1 | 16+16 add; no flags affected             |
| ED 8A    | PUSH nn         |     3 | Push 16-bit immediate; big-endian opcode |
| ED 90    | OUTINB          |     1 | OUT (C),mem[HL]; HL++; B unchanged       |
| ED 91    | NEXTREG n,v     |     2 | Write to NextREG via I/O                 |
| ED 92    | NEXTREG n,A     |     2 | Write A to NextREG via I/O               |
| ED 93    | PIXELDN         |     6 | Move HL down one pixel row               |
| ED 94    | PIXELAD         |     4 | Compute ULA address from D,E coords     |
| ED 95    | SETAE           |     4 | Set A to pixel mask from E               |
| ED 98    | JP (C)          |     3 | Jump via IN; needs configurable port     |
| ED A4    | LDIX            |     2 | LDI with transparency skip              |
| ED A5    | LDWS            |     3 | Load, L++, D++; flags from INC D        |
| ED AC    | LDDX            |     2 | LDD with transparency skip              |
| ED B4    | LDIRX           |     2 | Repeating LDIX; internal loop            |
| ED B6    | LDIRSCALE       |     1 | LDIRX with stride (BC' stride commented out in VHDL) |
| ED B7    | LDPIRX          |     2 | Pattern-fill with transparency           |
| ED BC    | LDDRX           |     1 | Repeating LDDX; internal loop            |
|          | **Total**       |  **85** |                                       |

## Special Handling

### I/O for NEXTREG and JP (C)

The `Z80NTestIO` class provides:

- **OUT capture**: Records all port writes. NEXTREG ED 91 writes register
  number to port 0x243B then value to port 0x253B. NEXTREG ED 92 does the
  same but reads the value from A. OUTINB (ED 90) writes `mem[HL]` to port C.
  All captured writes appear in the expected-output events.
- **Configurable IN**: JP (C) (ED 98) reads from port BC. The test harness
  pre-configures the return value so the jump target is deterministic.

### Repeating block instructions

LDIRX, LDDRX, LDPIRX, and LDIRSCALE loop internally until BC reaches zero.
The runner calls `cpu.execute()` once and expects it to return with BC=0 and
all bytes transferred. This matches the real hardware behaviour where these
instructions repeat without returning to the fetch cycle.

Single-step variants (LDIX, LDDX, LDWS) execute once per `cpu.execute()`
call, decrementing BC by 1.

### Flag behaviour

| Instruction group     | Flag effect                                         |
|-----------------------|-----------------------------------------------------|
| SWAPNIB, MIRROR A     | No flags affected                                  |
| TEST n                | Full flag set: S, Z, H=1, P (parity), N=0, C=0    |
| Barrel shifts/rotates | No flags affected                                  |
| MUL DE                | No flags affected                                  |
| ADD rr,A (31/32/33)   | C flag updated; S, Z, H, P/V, N preserved         |
| ADD rr,nn (34/35/36)  | No flags affected                                  |
| PUSH nn               | No flags affected                                  |
| OUTINB                | No flags affected (B unchanged, unlike OUTI)       |
| NEXTREG               | No flags affected                                  |
| PIXELDN, PIXELAD      | No flags affected                                  |
| SETAE                 | No flags affected                                  |
| JP (C)                | No flags affected                                  |
| LDIX, LDDX            | H=0, N=0, P/V set if BC-1 != 0                    |
| LDWS                  | Flags from INC D (S, Z, H, P/V, N)                |
| LDIRX, LDDRX          | H=0, N=0, P/V=0 (BC=0 after loop)                 |
| LDIRSCALE             | H=0, N=0, P/V=0                                   |
| LDPIRX                | H=0, N=0, P/V=0                                   |

### Undocumented flag bits

TEST n (ED 27) sets the undocumented X (bit 3) and Y (bit 5) flags from the
result of `A AND n`, matching the behaviour of AND on the base Z80. Test cases
verify these bits explicitly.

## CMake Integration

In `test/CMakeLists.txt`:

```cmake
# Z80N compliance test
add_executable(z80n_test z80n_test.cpp)
target_link_libraries(z80n_test PRIVATE jnext_core)

# Copy test data to build directory
file(COPY z80n/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/z80n)

# Register with CTest
add_test(NAME z80n_test
         COMMAND z80n_test ${CMAKE_CURRENT_BINARY_DIR}/z80n)
```

## Regression Integration

The `test/regression.sh` script gains a Z80N test phase that runs alongside
the existing FUSE phase:

```
=== JNEXT Regression Test Suite ===

[fuse-z80] Running FUSE Z80 opcode tests...
  PASS: 1356/1356 opcodes passed

[z80n]    Running Z80N extended opcode tests...
  PASS: 85/85 opcodes passed

Running screenshot tests...
  ...
```

The Z80N phase runs `build/test/z80n_test build/test/z80n` and checks the
exit code. Any failure is reported with the failing test name and
expected-vs-actual register diff.

## How to Run

```bash
# Build
cmake --build build -j$(nproc) 2>&1 | tail -5

# Run Z80N tests standalone
./build/test/z80n_test build/test/z80n

# Run full regression suite (includes Z80N)
bash test/regression.sh
```

Output format matches the FUSE runner: each failing test prints the test name,
expected state, and actual state. Exit code is 0 when all tests pass, 1
otherwise.
