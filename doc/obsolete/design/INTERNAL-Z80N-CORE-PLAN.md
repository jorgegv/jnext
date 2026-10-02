# Internal Z80N Core — Feasibility Assessment & Implementation Plan

## 1. Executive Summary

This document assesses replacing the current FUSE-derived Z80 core with a new
C++ Z80N core translated directly from the VHDL T80N core used in the ZX
Spectrum Next FPGA. The VHDL core is ~5,400 lines across 5 files and provides
cycle-exact behaviour including all Z80N extensions.

**Recommendation:** The project is feasible and worthwhile. Estimated effort is
4–6 weeks of focused development. The primary benefits are cycle-exact
compatibility with the real hardware, native Z80N support, and elimination of
the FUSE C code and its associated quirks.

---

## 2. Current State Analysis

### 2.1 Current FUSE-based Core

| Aspect    | Details                                                                                                                             |
|-----------|-------------------------------------------------------------------------------------------------------------------------------------|
| Origin    | FUSE emulator Z80 core (C, GPLv2)                                                                                                   |
| Files     | `third_party/fuse-z80/` — `fuse_z80_core.c`, `opcodes_base.c`, `z80_cb.c`, `z80_ed.c`, `z80_ddfd.c`, `z80_ddfdcb.c`, `z80_macros.h` |
| Wrapper   | `src/cpu/z80_cpu.{h,cpp}` — `Z80Cpu` class                                                                                          |
| Z80N      | Bolted on separately in `src/cpu/z80n_ext.{h,cpp}` via opcode interception                                                          |
| Interface | `MemoryInterface` (read/write), `IoInterface` (in/out), callbacks for M1, contention                                                |
| State     | Global `processor z80` struct, global `tstates` counter                                                                             |
| Testing   | 1356/1356 FUSE Z80 tests pass (100%), Z80N compliance tests in progress                                                             |

**Known issues with FUSE core:**
1. Global mutable state — not re-entrant, hard to unit-test in isolation
2. Z80N instructions intercepted *outside* the core, requiring register
   sync round-trips
3. Contention applied via macros referencing external global tables
4. I/O port width: only passes 8-bit C register, not full 16-bit BC
   (fixed via workaround)
5. No native support for Z80N prefix (ED xx) — all bolted on externally
6. Undocumented flag behaviour was difficult to verify against FUSE's
   own test suite expectations vs. real hardware behaviour
7. T-state counting required manual patches (readbyte/writebyte/port
   missing T-states discovered during Task 2)

### 2.2 VHDL T80N Core (Authoritative Hardware Spec)

| File             | Lines     | Purpose                                                        |
|------------------|-----------|----------------------------------------------------------------|
| `t80n.vhd`       | 1,802     | Main CPU: state machine, registers, bus control, Z80N dispatch |
| `t80n_mcode.vhd` | 2,644     | Microcode decoder: opcode → control signals per MCycle/TState  |
| `t80n_alu.vhd`   | 364       | ALU: arithmetic, logic, rotations, bit ops, DAA, RLD/RRD       |
| `t80na.vhd`      | 359       | Asynchronous bus wrapper (not needed for emulator)             |
| `t80n_pack.vhd`  | 267       | Package declarations, Z80N_seq enumeration                     |
| **Total**        | **5,436** |                                                                |

**Key architectural observations:**

- **MCycle/TState model:** Instructions decompose into 1–7 machine cycles,
  each with 1–6 T-states. The microcode ROM (`t80n_mcode`) outputs control
  signals for every (opcode, MCycle, TState) triplet.

- **Register file:** 8 register pairs stored as arrays (`RegsH[0..7]`,
  `RegsL[0..7]`), with separate address ports for A/B/C operands and
  direct-write ports for Z80N register manipulation.

- **ALU:** Purely combinational (no state). Takes BusA, BusB, flags,
  operation code → produces result and new flags. Clean separation makes
  it trivially portable to C++.

- **Prefix handling:** `ISet` (2-bit) tracks prefix state:
  - `00` = base, `01` = CB, `10` = ED, `11` = DD/FD prefix active
  - `XY_State` (2-bit) tracks IX/IY substitution

- **Z80N extensions:** Decoded in `t80n_mcode` as `Z80N_command_o` signal
  (enum). The main core handles some internally (register writes via
  direct ports), while others (MMU, NEXTREG) emit commands to the
  external system via `Z80N_command_o` + `Z80N_data_o`.

- **Bus protocol:** Synchronous internally (CEN-gated rising edge). The
  `t80na` async wrapper generates MREQ_n/IORQ_n/RD_n/WR_n edges — this
  wrapper is NOT needed for emulation (we call read/write directly).

- **Wait states:** `WAIT_n` input stalls TState progression. In emulation,
  this maps to contention delays added before memory/IO operations.

- **Interrupt model:** NMI edge-detected (NMI_s latch), INT level-sensitive
  sampled at end of instruction. IM 0/1/2 all supported. IFF1/IFF2
  flip-flops managed in the main state machine.

---

## 3. Translation Strategy

### 3.1 Approach: Structural Translation (not cycle-level simulation)

We will NOT simulate the VHDL at gate level. Instead, we perform a
**structural translation** that preserves the logical behaviour:

1. **Microcode table** → C++ lookup function returning a struct of control
   signals for each (opcode, ISet, MCycle, F, NMICycle, IntCycle, XY_State)
   input combination.

2. **ALU** → Pure C++ function: `alu_execute(op, busA, busB, flags_in) →
   (result, flags_out)`. Direct port of `t80n_alu.vhd`.

3. **Main state machine** → `step()` method that advances one T-state,
   consulting microcode outputs to determine bus operations, register
   transfers, and ALU invocations.

4. **Instruction-level API** → `execute_one()` method that runs all
   MCycles/TStates for one instruction and returns total T-states consumed.
   This is what the emulator main loop calls.

### 3.2 Why Not Cycle-Level Simulation?

Cycle-level (one `step()` per T-state) is possible but unnecessary for our
emulator because:
- Memory reads/writes happen at known T-state positions (documented in
  microcode), so we can batch them
- Contention delays are computed, not stalled via WAIT_n
- No DMA bus arbitration at T-state granularity (handled at instruction
  boundary)
- Performance: instruction-level execution is ~4x faster than T-state stepping

However, the internal state machine should still track MCycle/TState for
accurate T-state counting and contention point identification.

### 3.3 Alternative: T-State Level Step (Future Option)

For maximum accuracy (e.g., racing-the-beam effects), a `step_tstate()`
method could be added later. The microcode table supports this naturally.
The initial implementation will expose both `execute_one()` (fast, for
normal operation) and potentially `step_tstate()` (accurate, for debugging).

---

## 4. Detailed Component Design

### 4.1 File Structure

```
src/cpu/
├── cpu_interface.h       # Abstract base class (shared by both cores)
├── z80_cpu.h             # Existing FUSE core (now inherits CpuInterface)
├── z80_cpu.cpp
├── z80n_core.h           # New VHDL-derived core (inherits CpuInterface)
├── z80n_core.cpp          # Main state machine, execute_one()
├── z80n_microcode.h       # Microcode control signal struct
├── z80n_microcode.cpp     # Microcode decode (from t80n_mcode.vhd)
├── z80n_alu.h             # ALU function declarations
├── z80n_alu.cpp           # ALU (from t80n_alu.vhd)
├── z80n_registers.h       # Register file struct
└── z80n_types.h           # Shared types, enums, constants
```

### 4.2 Register File (`z80n_registers.h`)

Directly mirrors VHDL register set:

```cpp
struct Z80NRegisters {
    // Main register pairs (as in VHDL RegsH/RegsL arrays)
    uint8_t B, C, D, E, H, L;       // General purpose
    uint8_t A, F;                     // Accumulator + flags
    uint8_t B2, C2, D2, E2, H2, L2; // Alternate set
    uint8_t A2, F2;                   // Alternate AF

    uint16_t IX, IY;     // Index registers
    uint16_t SP;         // Stack pointer
    uint16_t PC;         // Program counter
    uint8_t  I;          // Interrupt vector base
    uint8_t  R;          // Refresh counter (7 bits + bit 7)
    uint16_t WZ;         // Internal MEMPTR (TmpAddr in VHDL)

    // State
    uint8_t IFF1, IFF2;  // Interrupt flip-flops
    uint8_t IM;          // Interrupt mode (0, 1, 2)
    bool halted;         // HALT state

    // Internal (from VHDL)
    uint8_t IR;          // Instruction register
    uint8_t ISet;        // Instruction set (prefix state: 0=base,1=CB,2=ED)
    uint8_t XY_State;    // IX/IY state (0=none, 1=IX, 2=IY)

    // Helper accessors
    uint16_t AF() const { return (A << 8) | F; }
    uint16_t BC() const { return (B << 8) | C; }
    uint16_t DE() const { return (D << 8) | E; }
    uint16_t HL() const { return (H << 8) | L; }
    void set_AF(uint16_t v) { A = v >> 8; F = v & 0xFF; }
    void set_BC(uint16_t v) { B = v >> 8; C = v & 0xFF; }
    void set_DE(uint16_t v) { D = v >> 8; E = v & 0xFF; }
    void set_HL(uint16_t v) { H = v >> 8; L = v & 0xFF; }
};
```

### 4.3 Microcode Control Signals (`z80n_microcode.h`)

Directly translated from `t80n_mcode.vhd` port list:

```cpp
struct MicrocodeOutput {
    uint8_t MCycles;        // Total machine cycles for this instruction
    uint8_t TStates;        // T-states for current machine cycle

    // Bus control
    bool Inc_PC;
    bool Inc_WZ;
    uint8_t IncDec_16;      // 4 bits: which pair, inc/dec
    bool Read_To_Reg;
    bool Read_To_Acc;
    uint8_t Set_BusA_To;    // 4 bits: source selector
    uint8_t Set_BusB_To;    // 4 bits: source selector
    uint8_t ALU_Op;         // 4 bits: ALU operation
    bool Save_ALU;
    bool PreserveC;
    bool Arith16;
    uint8_t Set_Addr_To;    // 3 bits: address source
    bool IORQ;
    bool Jump, JumpE, JumpXY;
    bool Call, RstP;
    bool LDZ, LDW, LDSPHL;
    uint8_t Special_LD;     // 3 bits
    bool ExchangeDH, ExchangeRp, ExchangeAF, ExchangeRS, ExchangeWH;
    bool I_DJNZ, I_CPL, I_CCF, I_SCF, I_RETN;
    bool I_BT, I_BC, I_BTR, I_RLD, I_RRD, I_INRC;
    bool SetDI, SetEI;
    uint8_t IMode;          // 2 bits
    bool Halt;
    bool NoRead, Write;
    bool No_PC;
    uint8_t Prefix;         // 2 bits: None, CB, ED, DD/FD
    bool XYbit_undoc;

    // Z80N extensions
    Z80NCommand z80n_command;  // enum: NONE, MMU, NEXTREG, MUL_DE, etc.
    uint8_t z80n_data;
    bool z80n_dout;
};

MicrocodeOutput decode(uint8_t IR, uint8_t ISet, uint8_t MCycle,
                       uint8_t F, bool NMICycle, bool IntCycle,
                       uint8_t XY_State);
```

The `decode()` function is a direct translation of the VHDL process in
`t80n_mcode.vhd` — a large switch/case on ISet, then IR, then MCycle.

### 4.4 ALU (`z80n_alu.h`)

```cpp
struct ALUResult {
    uint8_t Q;       // Result byte
    uint8_t F_Out;   // New flags
};

ALUResult alu_execute(uint8_t ALU_Op, uint8_t IR_bits,
                      uint8_t ISet, uint8_t BusA, uint8_t BusB,
                      uint8_t F_In, bool Arith16, bool Z16);
```

This is a direct, mechanical port of the 364-line `t80n_alu.vhd`. The VHDL
is entirely combinational (no clocked process), so it maps 1:1 to a pure
function.

### 4.5 Main Core Class (`z80n_core.h`)

```cpp
class Z80NCore {
public:
    Z80NCore(MemoryInterface& mem, IoInterface& io);

    void reset();
    int execute_one();  // Returns T-states consumed

    Z80NRegisters& registers() { return regs_; }
    const Z80NRegisters& registers() const { return regs_; }

    void request_interrupt(uint8_t vector);
    void request_nmi();
    bool is_halted() const;

    MemoryInterface& memory() { return mem_; }
    IoInterface& io() { return io_; }

    // Callbacks (same interface as current Z80Cpu)
    std::function<void(uint16_t addr)> on_contention;
    std::function<void(uint16_t pc)> on_m1_prefetch;
    std::function<void(uint16_t pc, uint8_t opcode)> on_m1_cycle;
    std::function<bool(uint16_t pc)> on_magic_breakpoint;

    void save_state(StateWriter& w) const;
    void load_state(StateReader& r);

private:
    MemoryInterface& mem_;
    IoInterface& io_;
    Z80NRegisters regs_;

    // Interrupt state
    bool int_pending_ = false;
    bool nmi_pending_ = false;
    bool nmi_edge_   = false;
    uint8_t int_vector_ = 0xFF;

    // Internal execution helpers
    uint8_t read_byte(uint16_t addr);   // Memory read + 3T + contention
    void write_byte(uint16_t addr, uint8_t val);
    uint8_t read_port(uint16_t port);   // I/O read + timing
    void write_port(uint16_t port, uint8_t val);
    uint8_t fetch_opcode();             // M1 read + 4T + contention

    void execute_z80n_command(Z80NCommand cmd, uint16_t data);
};
```

### 4.6 Execute Loop (Instruction-Level)

```cpp
int Z80NCore::execute_one() {
    if (halted) {
        // Execute NOP timing, check for interrupt wake
        return 4;
    }

    int total_tstates = 0;

    // Fetch + decode first byte
    uint8_t opcode = fetch_opcode();  // M1 cycle: 4T
    total_tstates += 4;
    regs_.IR = opcode;
    regs_.ISet = 0;

    // Handle prefixes (CB, ED, DD, FD)
    // ... prefix loop ...

    // Get microcode for all remaining machine cycles
    for (int mc = 2; mc <= mcycles; mc++) {
        auto mc_out = decode(regs_.IR, regs_.ISet, mc, regs_.F, ...);

        // Execute bus operations per TState count
        if (mc_out.Set_Addr_To != aNone) {
            // Set address bus from selected register pair
        }
        if (!mc_out.NoRead) {
            // Read byte from address bus
        }
        if (mc_out.Write) {
            // Write byte to address bus
        }
        if (mc_out.IORQ) {
            // I/O read or write
        }
        if (mc_out.Save_ALU) {
            auto [result, flags] = alu_execute(...);
            // Store result
        }
        // Register transfers, jumps, calls, etc.

        total_tstates += mc_out.TStates;
    }

    // Handle Z80N command if decoded
    if (z80n_cmd != Z80NCommand::NONE) {
        execute_z80n_command(z80n_cmd, z80n_data);
    }

    return total_tstates;
}
```

---

## 5. Interface Compatibility

### 5.1 Runtime Core Selection via Abstract Interface

Both cores must coexist permanently in the executable and be selectable at
runtime via CLI option (`--cpu-core fuse|internal`) and the GUI (Settings menu).
This is achieved by extracting the common interface into an abstract base
class:

```cpp
// src/cpu/cpu_interface.h
class CpuInterface {
public:
    virtual ~CpuInterface() = default;

    virtual void reset() = 0;
    virtual int  execute() = 0;  // Execute one instruction; returns T-states

    virtual Z80Registers get_registers() const = 0;
    virtual void set_registers(const Z80Registers& r) = 0;

    virtual void request_interrupt(uint8_t vector) = 0;
    virtual void request_nmi() = 0;
    virtual bool is_halted() const = 0;

    virtual MemoryInterface& memory() = 0;
    virtual IoInterface& io() = 0;

    virtual void save_state(StateWriter& w) const = 0;
    virtual void load_state(StateReader& r) = 0;

    // Callbacks — owned by the interface, wired by the emulator
    std::function<void(uint16_t addr)> on_contention;
    std::function<void(uint16_t pc)> on_m1_prefetch;
    std::function<void(uint16_t pc, uint8_t opcode)> on_m1_cycle;
    std::function<bool(uint16_t pc)> on_magic_breakpoint;
};
```

The emulator owns both core instances and a pointer to the active one:

```cpp
// In emulator.h
class Emulator {
    Z80Cpu             cpu_fuse_;     // FUSE-based core
    Z80NCore           cpu_internal_; // Internal Z80N core (from VHDL)
    CpuInterface*      cpu_ = nullptr; // Active core (points to one of above)
    // ...
public:
    CpuInterface& cpu() { return *cpu_; }
    void set_cpu_core(CpuCoreType type);  // Switch at runtime
};
```

**Runtime switching** (`set_cpu_core()`):
1. Transfer register state from old core to new: `new->set_registers(old->get_registers())`
2. Re-wire all callbacks (`on_m1_prefetch`, etc.) to the new core
3. Update `cpu_` pointer
4. Can be called at any instruction boundary (between frames is safest)

**CLI:** `--cpu-core fuse` (default during development), `--cpu-core internal`

**GUI:** Settings → Emulation → CPU Core → dropdown [FUSE Z80 / Internal Z80N]

Both `Z80Cpu` and `Z80NCore` inherit from `CpuInterface` and implement all
virtual methods. The existing `Z80Registers` struct is shared by both cores
as the external register representation.

### 5.2 Interface Mapping

| Current (`Z80Cpu`)     | New (`Z80NCore`)       | Notes                            |
|------------------------|------------------------|----------------------------------|
| `execute()`            | `execute_one()`        | Same semantics, returns T-states |
| `get_registers()`      | `registers()`          | Returns ref, no copy needed      |
| `set_registers(r)`     | `registers() = r`      | Direct assignment                |
| `request_interrupt(v)` | `request_interrupt(v)` | Identical                        |
| `request_nmi()`        | `request_nmi()`        | Identical                        |
| `is_halted()`          | `is_halted()`          | Identical                        |
| `memory()`             | `memory()`             | Same `MemoryInterface&`          |
| `io()`                 | `io()`                 | Same `IoInterface&`              |
| `on_contention`        | `on_contention`        | Same callback signature          |
| `on_m1_prefetch`       | `on_m1_prefetch`       | Same callback signature          |
| `on_m1_cycle`          | `on_m1_cycle`          | Same callback signature          |
| `on_magic_breakpoint`  | `on_magic_breakpoint`  | Same callback signature          |
| `save_state(w)`        | `save_state(w)`        | Same serialization interface     |
| `load_state(r)`        | `load_state(r)`        | Same deserialization interface   |

### 5.3 Contention Integration

The FUSE core uses global tables (`ula_contention[]`, `memory_map_read[]`)
with macros embedded in the opcode implementations. The new core handles
contention differently:

- Each `read_byte()`, `write_byte()`, `read_port()`, `write_port()` call
  includes contention as part of the T-state count
- Contention lookup uses the same `ContentionModel` class already in the
  emulator
- The microcode tells us *when* during the instruction each bus access
  happens, so we can compute the exact T-state position for contention
  lookup

### 5.4 Z80N Extensions

Currently bolted on externally (`z80n_ext.cpp`). In the new core, Z80N
instructions are decoded natively by the microcode (same as the VHDL), and
their execution is handled internally or via `Z80N_command_o` dispatch to
the emulator. This eliminates the register sync overhead and ensures
perfect timing.

---

## 6. Subsystem Interface Requirements

### 6.1 Memory Subsystem

No changes needed. The new core uses the existing `MemoryInterface`
(implemented by `Mmu`). The core calls `mem_.read(addr)` and
`mem_.write(addr, val)` at the appropriate points in the instruction
execution.

### 6.2 I/O Port Subsystem

No changes needed. The new core uses the existing `IoInterface`
(implemented by `PortDispatch`). Full 16-bit port address is available
natively (unlike FUSE where we had to reconstruct BC from the 8-bit C
value).

### 6.3 Interrupt Controller

No changes needed. The existing `Im2Controller` raises interrupts via
`request_interrupt()` and `request_nmi()` — same API.

### 6.4 DivMMC Automap

The `on_m1_prefetch` and `on_m1_cycle` callbacks continue to work
identically. DivMMC automap checks happen at the same points.

### 6.5 Debugger

The debugger accesses registers via `get_registers()`/`set_registers()`.
The new core exposes the same data. Additionally, the VHDL-derived core
provides `MCycle` and `TState` counters that could enable T-state-level
debugging in the future.

### 6.6 State Save/Load

Same serialization format. The new core has slightly more internal state
(ISet, XY_State, MCycle tracking) that should be serialized for mid-
instruction save/restore accuracy.

### 6.7 RZX Recording/Playback

RZX hooks into I/O reads. Since the new core uses the same `IoInterface`,
RZX intercepts work unchanged.

### 6.8 Backward Execution

Backward execution snapshots CPU state at instruction boundaries. The new
core's `Z80NRegisters` struct can be directly snapshotted, same as the
current `Z80Registers` struct.

---

## 7. Testing Strategy

### 7.1 Test Infrastructure: Dual-Core Support

All CPU test harnesses must be adapted to test both core implementations.
The approach:

**Test runner parameterization:**
```cpp
// test/cpu_test_common.h
enum class CpuCoreType { FUSE, INTERNAL };

// Factory that creates the appropriate core for testing
std::unique_ptr<CpuInterface> create_test_cpu(
    CpuCoreType type, MemoryInterface& mem, IoInterface& io);

// Google Test parameterized fixture
class CpuTest : public ::testing::TestWithParam<CpuCoreType> {
protected:
    void SetUp() override {
        cpu_ = create_test_cpu(GetParam(), mem_, io_);
    }
    std::unique_ptr<CpuInterface> cpu_;
    TestMemory mem_;
    TestIo io_;
};

// Each test suite runs twice — once per core
INSTANTIATE_TEST_SUITE_P(FuseCore, CpuTest,
    ::testing::Values(CpuCoreType::FUSE));
INSTANTIATE_TEST_SUITE_P(InternalCore, CpuTest,
    ::testing::Values(CpuCoreType::INTERNAL));
```

**Test suites requiring adaptation:**

| Test Suite             | File(s)                  | Adaptation                                                      |
|------------------------|--------------------------|-----------------------------------------------------------------|
| FUSE Z80 opcode tests  | `test/fuse_z80_test.cpp` | Parameterize on `CpuCoreType`; both cores must pass 1356/1356   |
| Z80N compliance tests  | `test/z80n_test.cpp`     | Parameterize on `CpuCoreType`; same expected values for both    |
| Z80N subsystem tests   | `test/z80n_compliance/`  | Already uses `CpuInterface` if built against abstract base      |
| Regression screenshots | `test/regression.sh`     | Run entire suite twice: `--cpu-core fuse` and `--cpu-core internal` |

**CLI for test scripts:**
```bash
# Run FUSE Z80 tests against both cores
./build/test/fuse_z80_test --core=fuse build/test/fuse
./build/test/fuse_z80_test --core=internal build/test/fuse

# Run regression screenshots against both cores
bash test/regression.sh --cpu-core fuse
bash test/regression.sh --cpu-core internal
```

**Separate reference screenshots:** The two cores may produce subtly
different timing, leading to different screenshots for timing-sensitive
tests. Reference images are stored per-core:
```
test/reference/fuse/     # References for FUSE core
test/reference/internal/ # References for Internal core (may differ)
```

### 7.2 Unit Tests (New Core Only)

- **ALU tests:** Exhaustive test of all ALU operations against VHDL
  golden values (can auto-generate from VHDL simulation)
- **Microcode tests:** Verify control signal outputs for representative
  instructions match VHDL decode tables
- **Individual instruction tests:** Test each opcode in isolation for
  correct register/memory/flag changes and T-state count

### 7.3 Integration Tests (Both Cores)

- **FUSE Z80 test suite (1356 tests):** Must pass 100% on both cores —
  this is the primary correctness gate
- **Z80N compliance tests:** Already being developed (Task 1), run
  against both cores with identical expected values
- **Cross-validation:** Run both cores on same input sequences, compare
  register state and T-state counts instruction-by-instruction

### 7.4 System Tests (Both Cores)

- **Regression screenshot tests:** Run full suite with each core, compare
  against per-core reference images
- **Game compatibility:** Test with known timing-sensitive games on both
- **NextZXOS boot:** Must boot to the same point on both cores

### 7.5 Cross-Validation Mode

During development, a special validation mode runs both cores in parallel:
```
[Emulator] ─── execute() ──→ [FUSE Core]     → T-states, registers
            └── execute() ──→ [Internal Core] → T-states, registers
                                              ↓ compare
```

Enabled via `--cpu-cross-validate`. Logs divergences with full context
(PC, opcode, registers before/after, T-state counts). Divergences where
the new core matches VHDL behaviour and the FUSE core doesn't are
*expected* and should be documented as improvements.

---

## 8. Complexity Assessment

### 8.1 Component Effort Estimates

| Component          | VHDL Lines  | C++ Lines (est.) | Difficulty | Notes                                        |
|--------------------|-------------|------------------|------------|----------------------------------------------|
| ALU                | 364         | ~300             | Low        | Purely combinational, mechanical translation |
| Microcode          | 2,644       | ~2,200           | Medium     | Large but repetitive switch/case             |
| Registers          | ~50         | ~100             | Low        | Struct definitions                           |
| Main state machine | 1,802       | ~800             | High       | Most complex logic, prefix handling          |
| Z80N dispatch      | ~200        | ~150             | Low        | Already implemented in z80n_ext.cpp          |
| Bus interface      | 359 (t80na) | ~100             | Low        | Simplified for emulation                     |
| Integration/glue   | —           | ~200             | Medium     | Callbacks, contention wiring                 |
| **Total**          | **~5,400**  | **~3,850**       |            |                                              |

### 8.2 Risk Assessment

| Risk                              | Likelihood | Impact | Mitigation                                                  |
|-----------------------------------|------------|--------|-------------------------------------------------------------|
| Microcode translation errors      | Medium     | High   | FUSE test suite catches most; cross-validation catches rest |
| Undocumented flag behaviour wrong | Low        | Medium | FUSE tests cover all undocumented flags                     |
| Performance regression            | Low        | Low    | Instruction-level exec, no worse than FUSE                  |
| Z80N timing differences           | Medium     | Medium | Compare with VHDL simulation outputs                        |
| Prefix state machine bugs         | Medium     | High   | Targeted tests for DD/FD/CB prefix chains                   |
| Contention timing changes         | Low        | Medium | Regression screenshots catch visual differences             |

### 8.3 What Makes This Tractable

1. **The VHDL is well-structured:** Clear separation of ALU, microcode,
   and control logic. Each translates independently.
2. **We have a comprehensive test suite:** 1356 FUSE tests + Z80N tests +
   regression screenshots provide a strong safety net.
3. **The interface is already defined:** `MemoryInterface`, `IoInterface`,
   callbacks — all stay the same.
4. **Z80N extensions already work:** `z80n_ext.cpp` can be reused or its
   logic inlined.
5. **We can run both cores in parallel** during validation, catching bugs
   before they matter.

---

## 9. Benefits

### 9.1 Correctness
- Cycle-exact match with real hardware (same source as FPGA)
- Native Z80N instruction support (no external interception)
- Correct T-state counts derived from microcode, not manual patches
- Full 16-bit port address available natively

### 9.2 Maintainability
- Single source of truth: when VHDL changes, update C++ to match
- No global mutable state (encapsulated in `Z80NCore` class)
- Clean C++ vs. legacy C with macros
- Z80N instructions are part of the core, not bolted on

### 9.3 Debuggability
- MCycle/TState counters available for T-state-level debugging
- Internal state (ISet, XY_State, prefix) visible for diagnostics
- Potential for T-state stepping in the debugger

### 9.4 Licensing
- T80 core is BSD-licensed (OpenCores), cleaner than FUSE's GPLv2
- Our derivative C++ code would inherit BSD terms

---

## 10. Implementation Phases

### Phase 1: ALU + Microcode (Week 1–2)
- Translate `t80n_alu.vhd` → `z80n_alu.cpp`
- Write ALU unit tests (exhaustive for all ops)
- Translate `t80n_mcode.vhd` → `z80n_microcode.cpp`
- Write microcode spot-check tests

### Phase 2: Core State Machine (Week 2–3)
- Implement `Z80NCore::execute_one()` from `t80n.vhd`
- Handle all prefix states (CB, ED, DD/FD, DDCB/FDCB)
- Handle interrupt and NMI processing
- Handle HALT state

### Phase 3: Integration + Contention (Week 3–4)
- Wire to existing `MemoryInterface` / `IoInterface`
- Implement contention calls at correct T-state positions
- Wire callbacks (M1 prefetch, M1 cycle, magic breakpoint)
- Implement `save_state()` / `load_state()`

### Phase 4: Validation (Week 4–5)
- Pass all 1356 FUSE Z80 tests
- Pass all Z80N compliance tests
- Cross-validate against FUSE core (parallel execution mode)
- Run full regression screenshot test suite
- Test NextZXOS boot sequence

### Phase 0: Abstract Interface Extraction (Pre-requisite, ~1 day)
- Create `CpuInterface` abstract base class in `src/cpu/cpu_interface.h`
- Make `Z80Cpu` inherit from `CpuInterface`
- Change `Emulator` to hold `CpuInterface*` pointer instead of `Z80Cpu` value
- Add `--cpu-core` CLI option and GUI settings dropdown
- Verify all tests still pass with the indirection in place
- This phase can be done *before* any Z80N core work begins

### Phase 5: Integration & Coexistence (Week 5–6)
- Make `Z80NCore` inherit from `CpuInterface`
- Wire both cores into `Emulator` with runtime switching
- Implement register state transfer on core switch
- Default to FUSE core; `--cpu-core internal` enables new core
- Fix any remaining divergences
- Update documentation

---

## 11. Appendix: VHDL Signal → C++ Mapping Reference

### Microcode Outputs
| VHDL Signal      | C++ Field      | Meaning                              |
|------------------|----------------|--------------------------------------|
| `MCycles`        | `MCycles`      | Total machine cycles for instruction |
| `TStates`        | `TStates`      | T-states for current machine cycle   |
| `Inc_PC`         | `Inc_PC`       | Increment program counter            |
| `Set_BusA_To`    | `Set_BusA_To`  | ALU input A source                   |
| `Set_BusB_To`    | `Set_BusB_To`  | ALU input B source                   |
| `ALU_Op`         | `ALU_Op`       | ALU operation select                 |
| `Save_ALU`       | `Save_ALU`     | Store ALU result to register         |
| `Set_Addr_To`    | `Set_Addr_To`  | Address bus source                   |
| `Read_To_Reg`    | `Read_To_Reg`  | Route data bus to register           |
| `Read_To_Acc`    | `Read_To_Acc`  | Route data bus to accumulator        |
| `IncDec_16`      | `IncDec_16`    | 16-bit inc/dec select                |
| `Prefix`         | `Prefix`       | Prefix type (None/CB/ED/DDFD)        |
| `Z80N_command_o` | `z80n_command` | Z80N extended op type                |

### Z80N Commands
| VHDL Enum   | Emulator Action                      |
|-------------|--------------------------------------|
| `MMU`       | Write NextREG 0x50-0x57              |
| `NEXTREGW`  | Write NextREG index+value            |
| `MUL_DE`    | D*E → DE (internal)                  |
| `ADD_HL_A`  | HL += A (internal)                   |
| `PIXELDN`   | Pixel row increment (internal)       |
| `PUSH_nn`   | Push 16-bit immediate (bus writes)   |
| `LDPIRX`    | Pattern load (bus reads/writes)      |
| `LDIRSCALE` | Scaled block load (bus reads/writes) |
| `JP_C`      | Jump via port read (bus + IO)        |

### Register File Addresses (VHDL AddrA/B/C encoding)
| Address | High   | Low    |
|---------|--------|--------|
| 000     | B      | C      |
| 001     | D      | E      |
| 010     | H      | L      |
| 011     | (temp) | (temp) |
| 100     | B'     | C'     |
| 101     | D'     | E'     |
| 110     | H'     | L'     |
| 111     | —      | —      |

---

## 12. Decision

**Proceed?** This is a significant but well-scoped effort with clear benefits.
The existing test infrastructure and the clean VHDL design make the risk
manageable. The ability to run both cores in parallel during validation
provides a strong safety net.

Both cores will remain permanently available in the executable, selectable
at runtime via `--cpu-core fuse|internal` or the GUI Settings menu. This allows
users to compare behaviour and provides a fallback if the new core exhibits
issues with specific software.

The key question is timing: this should be done when there is no other
high-priority feature work blocking, as it requires sustained focused
attention (particularly the microcode translation in Phase 1).
