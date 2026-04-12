# Copper Coprocessor Compliance Test Plan

VHDL-derived compliance test plan for the ZX Spectrum Next Copper coprocessor
subsystem. All behaviour specifications are taken exclusively from the FPGA
VHDL sources (`copper.vhd`, `zxnext.vhd`, `zxula_timing.vhd`).

## Purpose

The Copper is a simple display-synchronized coprocessor that executes a list of
up to 1024 16-bit instructions from dedicated dual-port RAM. It can issue WAIT
(synchronize to beam position) and MOVE (write a NextREG) instructions,
enabling raster-timed register changes without CPU involvement. Correct
emulation requires precise instruction decoding, timing, RAM access, and
interaction with the NextREG write arbitration.

## VHDL Reference Summary

### Instruction format (16-bit)

| Bit 15 | Bits 14..9       | Bits 8..0        | Type |
|--------|------------------|-------------------|------|
| 0      | reg[6:0] (7 bit) | data[7:0] (8 bit) | MOVE |
| 1      | hpos[5:0]        | vpos[8:0]         | WAIT |

- **MOVE**: Writes `data` to NextREG `{0, reg[6:0]}` (registers 0x00..0x7F).
  A MOVE with reg=0x00 (`0000000`) is a **NOP** -- no write pulse is generated
  but the address still advances.
- **WAIT**: Blocks until `vcount == vpos` AND `hcount >= (hpos << 3) + 12`.
  The horizontal comparison uses `hpos * 8 + 12`, giving 64 distinct horizontal
  positions across the scanline.

### Control modes (NextREG 0x62 bits 7..6)

| Mode | Bits | Behaviour |
|------|------|-----------|
| Stop | 00   | Copper halted; no instructions execute |
| Run  | 01   | Start executing from address 0; run once then stop |
| Loop | 11   | Start executing from address 0; restart at frame start (vcount=0, hcount=0) |
| (reserved) | 10 | Not used in VHDL; treated as "not 00" so copper runs |

### State machine (from `copper.vhd`)

Per 28 MHz clock cycle:

1. **Reset**: address = 0, dout = 0.
2. **Mode change detection**: When `copper_en_i` changes from its previous
   value, if new mode is `01` or `11`, address resets to 0 and dout clears.
3. **Loop restart**: In mode `11`, when `vcount=0` and `hcount=0`, address
   resets to 0.
4. **Instruction execution** (when mode != `00`):
   - If previous cycle was a MOVE (`copper_dout_s = 1`), clear dout (one-cycle
     write pulse).
   - If current instruction is WAIT (bit 15 = 1): compare beam position; if
     matched, advance address by 1.
   - If current instruction is MOVE (bit 15 = 0): output register+data, set
     dout high (unless NOP), advance address by 1.
5. **Stopped** (mode `00`): dout cleared.

### Instruction RAM (dual-port, 1024 x 16-bit)

Two 1024x8 dual-port RAMs (MSB and LSB), accessed:
- **Port A** (CPU writes): clocked at 28 MHz positive edge; addressed by
  `nr_copper_addr[10:1]` (the 10-bit instruction index).
- **Port B** (Copper reads): clocked at 28 MHz **negative** edge; addressed by
  `copper_instr_addr`.

### NextREG interface for RAM writes

| NextREG | Function |
|---------|----------|
| 0x60    | Write 8-bit data to copper RAM. On even byte (addr bit 0 = 0), stores MSB in `nr_copper_data_stored`. On odd byte, triggers write of both stored MSB and current LSB. Address auto-increments after every write. `nr_copper_write_8 = 1`. |
| 0x61    | Set copper instruction address low byte: `nr_copper_addr[7:0] = data` |
| 0x62    | Set copper mode and address high bits: `mode = data[7:6]`, `nr_copper_addr[10:8] = data[2:0]` |
| 0x63    | Write 16-bit data to copper RAM. Same store/write pattern as 0x60, but `nr_copper_write_8 = 0`. Address auto-increments. |
| 0x64    | Set copper vertical offset (`nr_64_copper_offset`) |

### RAM write enable logic

```
copper_msb_we = nr_copper_we AND (
    (nr_copper_write_8=0 AND addr[0]=1) OR    -- 16-bit mode, odd byte
    (nr_copper_write_8=1 AND addr[0]=0)        -- 8-bit mode, even byte
)
copper_lsb_we = nr_copper_we AND addr[0]=1    -- always on odd byte
```

For **NR 0x60** (8-bit mode, `nr_copper_write_8=1`):
- Even address: stores data in `nr_copper_data_stored`, MSB RAM written with
  current data, addr++.
- Odd address: LSB RAM written, addr++.
- Net effect: MSB written on even, LSB on odd. Two writes per instruction.

For **NR 0x63** (16-bit mode, `nr_copper_write_8=0`):
- Even address: stores data in `nr_copper_data_stored`, addr++.
- Odd address: MSB RAM written with stored data, LSB RAM written with current
  data, addr++.
- Net effect: first byte cached, second byte triggers simultaneous MSB+LSB
  write. Two writes per instruction.

### NextREG read-back

| NextREG | Read value |
|---------|------------|
| 0x61    | `nr_copper_addr[7:0]` |
| 0x62    | `{nr_62_copper_mode, "000", nr_copper_addr[10:8]}` |
| 0x64    | `nr_64_copper_offset` |

### Copper vertical offset (NextREG 0x64)

The copper vertical count (`cvc`) is offset from the true vertical count:
- At `ula_min_vactive` (start of active display), `cvc` resets to
  `{0, copper_offset}` instead of 0.
- This shifts the copper's notion of line 0 by the offset value.

### NextREG write arbitration

Copper has **highest priority** over CPU for NextREG writes:
- `nr_wr_en = copper_req OR cpu_req`
- `nr_wr_reg = copper_nr_reg` when `copper_req=1`, else `cpu_nr_reg`
- CPU request only clears when `copper_req=0` (copper can delay CPU writes).
- Copper register output is `{0, copper_dout[14:8]}` (7-bit, max reg 0x7F).

## Architecture

### Test runner

A dedicated `copper_test.cpp` test runner that:
1. Instantiates the Copper subsystem (or a minimal emulator context with copper
   + video timing).
2. Programs copper instruction RAM via NextREG 0x60/0x63 writes.
3. Advances the emulation clock cycle-by-cycle.
4. Captures NextREG write outputs and verifies timing against expected beam
   positions.

### Test data

Each test case specifies:
- Copper instruction list (address + 16-bit instruction pairs)
- Copper mode (NextREG 0x62 value)
- Optional copper offset (NextREG 0x64 value)
- Expected sequence of NextREG writes with their (hcount, vcount) timing
- Expected final copper state (address, mode)

## Test Case Catalog

### Group 1: Instruction RAM Access (NR 0x60, 0x61, 0x62, 0x63)

| ID    | Test | Description |
|-------|------|-------------|
| RAM-01 | NR 0x60 sequential write | Write two bytes via NR 0x60; verify MSB stored on even, LSB written on odd; instruction readable by copper |
| RAM-02 | NR 0x63 sequential write | Write two bytes via NR 0x63; verify first byte cached, second triggers MSB+LSB write |
| RAM-03 | NR 0x61 address set low | Set addr low byte via NR 0x61; verify addr[7:0] updated, addr[10:8] preserved |
| RAM-04 | NR 0x62 address set high + mode | Set mode and high addr via NR 0x62; verify mode[1:0] and addr[10:8] |
| RAM-05 | Address auto-increment | Write 4 bytes via NR 0x60; verify addr increments by 1 each write |
| RAM-06 | Address wrap-around | Set addr to 0x7FF (max), write 2 bytes; verify wrap to 0x000 |
| RAM-07 | Mixed 8/16-bit writes | Alternate NR 0x60 and NR 0x63 writes; verify correct RAM contents |
| RAM-08 | Read-back NR 0x61 | Write NR 0x61, read back; verify low address byte |
| RAM-09 | Read-back NR 0x62 | Write NR 0x62, read back; verify mode and high address bits |
| RAM-10 | Full RAM fill | Fill all 1024 instructions; verify last entry correct |

### Group 2: MOVE Instruction

| ID    | Test | Description |
|-------|------|-------------|
| MOV-01 | Basic MOVE | Single MOVE to NR 0x40 with value 0x55; verify NextREG write occurs |
| MOV-02 | MOVE register range | MOVE to register 0x01, 0x3F, 0x7F; verify register number in output |
| MOV-03 | MOVE data values | MOVE with data 0x00, 0xFF, 0xA5; verify data byte in output |
| MOV-04 | MOVE NOP (reg=0) | MOVE with reg=0x00 (NOP); verify NO write pulse generated, address still advances |
| MOV-05 | MOVE write pulse duration | Verify dout_en is high for exactly 1 clock cycle per MOVE |
| MOV-06 | Consecutive MOVEs | Two consecutive MOVE instructions; verify second executes on cycle after first's write-pulse clears |
| MOV-07 | MOVE timing | MOVE executes immediately (no wait); verify output on same cycle as instruction fetch |
| MOV-08 | MOVE output format | Verify copper_data_o = {reg[6:0], data[7:0]} (15 bits) |

### Group 3: WAIT Instruction

| ID    | Test | Description |
|-------|------|-------------|
| WAI-01 | WAIT exact match | WAIT for vpos=100, hpos=0; verify advance when vcount=100 and hcount >= 12 |
| WAI-02 | WAIT hpos threshold | WAIT with hpos=10; verify advance when hcount >= 10*8+12 = 92 |
| WAI-03 | WAIT hpos maximum | WAIT with hpos=63 (max); verify advance when hcount >= 63*8+12 = 516 (wraps in 9-bit counter) |
| WAI-04 | WAIT vpos only | WAIT with hpos=0; verify triggers at hcount >= 12 on the target line |
| WAI-05 | WAIT no advance before match | WAIT for vpos=50; verify copper stalls when vcount != 50 |
| WAI-06 | WAIT vcount must equal | WAIT for vpos=100; verify does NOT advance when vcount=101 (equality, not >=) |
| WAI-07 | WAIT hcount >= check | WAIT hpos=5; verify advances when hcount=52 (exactly 5*8+12) and also when hcount=60 (>=) |
| WAI-08 | WAIT then MOVE | WAIT followed by MOVE; verify MOVE executes on cycle after WAIT passes |
| WAI-09 | WAIT for line 0 | WAIT for vpos=0, hpos=0; verify triggers at start of frame |
| WAI-10 | WAIT max vpos | WAIT for vpos=511 (9-bit max); verify behaviour with maximum value |
| WAI-11 | Multiple WAITs | WAIT for line 50, then WAIT for line 100; verify sequential execution |
| WAI-12 | WAIT past end of line | WAIT hpos that produces hcount threshold > max hcount; verify behaviour |

### Group 4: Control Modes

| ID    | Test | Description |
|-------|------|-------------|
| CTL-01 | Mode 00 (Stop) | Set mode=00; verify no instructions execute, dout stays low |
| CTL-02 | Mode 01 (Run) | Set mode=01; verify execution starts from address 0 |
| CTL-03 | Mode 11 (Loop) | Set mode=11; verify execution starts from address 0 |
| CTL-04 | Mode 01 address reset | Switch from 00 to 01; verify address resets to 0 |
| CTL-05 | Mode 11 address reset | Switch from 00 to 11; verify address resets to 0 |
| CTL-06 | Mode 10 behaviour | Set mode=10; verify copper runs (treated as "not 00") but no loop restart |
| CTL-07 | Loop restart at frame | Mode=11; verify address resets to 0 when vcount=0, hcount=0 |
| CTL-08 | Loop restart timing | Mode=11; program MOVE instructions; verify they re-execute each frame |
| CTL-09 | Run does not loop | Mode=01; execute through list; verify does NOT restart at frame start |
| CTL-10 | Mode change mid-execution | Switch from 01 to 11 mid-list; verify address resets to 0 |
| CTL-11 | Stop while running | Switch from 01 to 00 mid-execution; verify copper halts, dout clears |
| CTL-12 | Same mode no reset | Write mode=01 when already 01; verify address does NOT reset (last_state unchanged) |
| CTL-13 | Mode change clears dout | Switch mode during a MOVE write pulse; verify dout clears |

### Group 5: Timing Accuracy

| ID    | Test | Description |
|-------|------|-------------|
| TIM-01 | MOVE takes 2 cycles | MOVE: cycle 1 sets dout high + data; cycle 2 clears dout. Verify address advances on cycle 1 |
| TIM-02 | WAIT no-op cycle | WAIT that doesn't match: verify no address advance, no output, 1 cycle consumed |
| TIM-03 | Copper runs at 28 MHz | Verify copper state machine is clocked at 28 MHz (one decision per clock) |
| TIM-04 | WAIT + MOVE pipeline | WAIT matches on cycle N; verify MOVE at addr+1 is fetched and executes on cycle N+1 |
| TIM-05 | Dual-port read timing | Copper reads on negative clock edge; verify instruction data is available on next positive edge |
| TIM-06 | Instruction throughput | Program 10 consecutive MOVEs; verify 10 NextREG writes occur in 20 cycles (2 cycles each) |
| TIM-07 | WAIT granularity | Verify WAIT horizontal resolution is 8-pixel steps (hpos field is 6 bits) |

### Group 6: Copper Vertical Offset (NR 0x64)

| ID    | Test | Description |
|-------|------|-------------|
| OFS-01 | Zero offset | Set NR 0x64=0; verify copper vcount starts at 0 at active display start |
| OFS-02 | Non-zero offset | Set NR 0x64=32; verify copper vcount starts at 32 at active display start |
| OFS-03 | WAIT with offset | Set offset=10; WAIT for vpos=10; verify triggers at first active scanline |
| OFS-04 | Offset read-back | Write NR 0x64=0x80; read back; verify 0x80 returned |
| OFS-05 | Offset reset | Verify NR 0x64 resets to 0x00 on system reset |
| OFS-06 | CVC wraps at max_vc | Verify copper vertical counter wraps at timing-dependent max_vc value |

### Group 7: NextREG Write Arbitration

| ID    | Test | Description |
|-------|------|-------------|
| ARB-01 | Copper priority | Copper and CPU write simultaneously; verify copper write takes priority |
| ARB-02 | CPU delayed by copper | Copper write active; CPU write pending; verify CPU write completes after copper clears |
| ARB-03 | Copper write pulse | Verify copper_req is generated on rising edge of copper_dout_en |
| ARB-04 | Copper register masking | Copper MOVE to reg 0x7F; verify nr_wr_reg = 0x7F (bit 7 forced to 0) |
| ARB-05 | No copper interference when stopped | Mode=00; verify copper never asserts req, CPU writes proceed immediately |

### Group 8: Reset Behaviour

| ID    | Test | Description |
|-------|------|-------------|
| RST-01 | Hard reset state | After reset: address=0, dout=0, data=0 |
| RST-02 | Mode reset | After reset: NR 0x62 copper_mode = 00 |
| RST-03 | Address reset | After reset: nr_copper_addr = 0 |
| RST-04 | Offset reset | After reset: nr_64_copper_offset = 0 |
| RST-05 | Stored data reset | After reset: nr_copper_data_stored = 0x00 |

### Group 9: Edge Cases and Corner Cases

| ID    | Test | Description |
|-------|------|-------------|
| EDG-01 | End of instruction RAM | Execute through all 1024 entries; verify address wraps to 0 (10-bit counter) |
| EDG-02 | WAIT for already-passed line | WAIT for line 10 when vcount is 50; verify copper stalls until next frame (loop mode) or forever (run mode) |
| EDG-03 | MOVE to copper's own registers | MOVE to NR 0x62 (copper mode); verify copper can modify its own mode |
| EDG-04 | MOVE to NR 0x60 | Copper writes to NR 0x60 (copper data); verify copper can self-modify its instruction RAM |
| EDG-05 | All-WAIT program | 1024 WAIT instructions; verify copper stalls at first non-matching WAIT |
| EDG-06 | All-NOP program | 1024 NOP instructions (MOVE reg=0); verify all execute with no write pulses |
| EDG-07 | WAIT hpos=0 matches at hcount=12 | Verify the +12 offset in horizontal comparison |
| EDG-08 | Rapid mode toggling | Toggle mode 00->01->00->11 rapidly; verify address reset behaviour on each transition |
| EDG-09 | Copper + NMI generation | Copper writes NR 0x02 (NMI control); verify nmi_cu_02_we signal fires |

## Special Handling

### Beam position simulation

Tests require a controllable beam position (hcount, vcount). The test harness
must either:
- Drive hcount/vcount directly via a mock video timing module, or
- Step the full timing engine and verify copper behaviour at known positions.

The first approach (mock) is preferred for unit-level tests; the second for
integration tests.

### Dual-clock domain

The VHDL uses positive-edge for the copper state machine and negative-edge for
instruction RAM reads. The emulator likely uses a single-clock model. Tests
should verify that instruction data is available when the copper needs it
(no off-by-one from clock domain crossing).

### Write arbitration

Tests in Group 7 require both copper and CPU to attempt NextREG writes on the
same cycle. The test harness needs a way to force simultaneous write requests.

## File Layout

```
test/
  copper/
    copper_test.cpp           # Test runner executable
    test_cases.h              # Test case definitions (or inline)
  CMakeLists.txt              # Updated: copper_test executable + CTest
doc/design/
  COPPER-TEST-PLAN-DESIGN.md  # This document
```

## CMake Integration

```cmake
# Copper compliance test
add_executable(copper_test copper/copper_test.cpp)
target_link_libraries(copper_test PRIVATE jnext_core)

# Register with CTest
add_test(NAME copper_test COMMAND copper_test)
```

## How to Run

```bash
# Build
cmake --build build -j$(nproc) 2>&1 | tail -5

# Run copper tests standalone
./build/test/copper_test

# Run full regression suite (includes copper)
bash test/regression.sh
```

## Test Count Summary

| Group | Tests | Description |
|-------|------:|-------------|
| 1. RAM Access        | 10 | Instruction RAM read/write via NextREGs |
| 2. MOVE              |  8 | MOVE instruction execution |
| 3. WAIT              | 12 | WAIT instruction beam matching |
| 4. Control Modes     | 13 | Stop/Run/Loop/Reset mode switching |
| 5. Timing            |  7 | Cycle-accurate timing verification |
| 6. Vertical Offset   |  6 | Copper offset (NR 0x64) |
| 7. Arbitration       |  5 | CPU vs copper NextREG priority |
| 8. Reset             |  5 | Power-on/reset state |
| 9. Edge Cases        |  9 | Corner cases and self-modification |
| **Total**            | **75** | |
