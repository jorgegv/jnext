# NextREG Compliance Test Plan

VHDL-derived compliance test plan for the NextREG (TBBlue Register) subsystem
of the JNEXT emulator, covering register selection, read/write semantics,
reset defaults, read-only registers, and copper/CPU arbitration.

## Purpose

Validate that the emulator's NextREG implementation matches the VHDL behaviour
defined in `zxnext.vhd` (lines ~4585-6293), specifically the register
select mechanism (port 0x243B), data read/write (port 0x253B), reset defaults,
register encoding, and arbitration between CPU and copper requesters.

## Current status

Rewrite in Phase 2 per-row idiom merged on main 2026-04-15 (`task1-wave2-nextreg`).

Measured on main 2026-04-20 post-Phase-1b fix:

- **66 plan rows total** (added CLIP-09/10 on 2026-04-20 Phase-2-E for the NR 0x1B read-handler un-skip round).
- **Bare test** (`test/nextreg/nextreg_test.cpp`): 21 pass / 0 fail / 35 skip. Reduced from 45 skip by re-homing 10 integration-tier rows (RST-01..08, MMU-01, PE-04) as source comments pointing at `nextreg_integration_test.cpp`.
- **Integration test** (`test/nextreg/nextreg_integration_test.cpp`): 53 pass / 0 fail / 13 skip out of 66 rows. Added 28 integration rows during Phase 1b covering RO-01..06, SEL-03, CLIP-01..08, PAL-01..06, PE-05, RW-01..02, CFG-01/02/05. After 2026-04-20 critic pass, 3 palette rows (PAL-01, PAL-03, PAL-06) converted from SKIP → real FAIL per §2 (facility exists in `src/video/palette.cpp`; converting to SKIP would have hidden real emulator bugs — coverage-theatre anti-pattern). RO-04 and CFG-01 converted from pass/tautology → SKIP with VHDL-correct backlog notes. Phase-2-E (2026-04-20) installed NR 0x1B read handler (combinatorial mux over tilemap clip coords — zxnext.vhd:5971-5977) and un-skipped RST-09 + CLIP-07 (CLIP-07 was a stale skip — NR 0x1C read handler had already been installed in Phase 1b); added CLIP-09/10 as discriminative rows for the read-vs-write idx-advance invariant.
- **Emulator Bug backlog items surfaced**: PAL-01 (auto-inc advance broken), PAL-03 (NR 0x44 9-bit sub_idx or test-harness issue), PAL-06 (NR 0x43 bit 7 auto-inc-disable not effective). PE-05 (NR 0x89 default should be 0x8F not 0xFF — layout matches NR 0x85 per VHDL:6147-6150). RO-04 (VHDL g_sub_version=0x03, JNEXT leaves regs_[0x0E]=0x00). CFG-01 (NR 0x03 read is composed, not raw register). CFG-02 (dt_lock XOR not modelled). RO-02/SEL-03 (NR 0x00 RO enforcement — regs_[0] round-trips writes). CLIP-01..05 (Layer2/Sprite/Ula public clip getters + `Emulator::ula()` accessor missing). CLIP-08 (NR 0x18 Layer2 read handler missing — combinatorial mux per zxnext.vhd:5947-5953). RW-01/02 (NR 0x07/0x08 read handlers missing).
- **Matrix-tooling gap:** `test/refresh-traceability-matrix.py` does not scan `nextreg_integration_test.cpp`, so Phase 1b integration rewrites are invisible to the per-row matrix table and the 3 FAILs above are not counted in the aggregate. Tracked in TRACEABILITY-MATRIX.md. Backlog: extend refresh script.
- **(D) plan nit**: plan mixes bare-tier rows with integration-tier rows without labelling. Future plan refresh should tag each row with its intended test tier (bare / subsystem / integration). Test file's skip reasons document the tier boundary inline.

## Authoritative VHDL Source

`zxnext.vhd`:
- Lines 4585-4603: Register selection via port 0x243B
- Lines 4706-4777: Write arbitration (copper priority over CPU)
- Lines 4782-4912: Write register dispatch (combinatorial `nr_*_we` signals)
- Lines 4926-5800: Register state and write handling
- Lines 5867-6292: Read registry (port 0x253B read dispatch)

## Architecture

### Register Access Mechanism

From `zxnext.vhd` lines 4589-4603:

1. **Port 0x243B write**: Sets `nr_register` to `cpu_do`. On reset, `nr_register` is set to `0x24` as protection against legacy programs.
2. **Port 0x243B read**: Returns current `nr_register` value.
3. **Port 0x253B write**: Triggers a write to the register selected by `nr_register`.
4. **Port 0x253B read**: Returns the value of the register selected by `nr_register`.

### Write Sources (lines 4706-4777)

Three sources can write NextREGs:
1. **Z80N NEXTREG instruction** (ED 91/ED 92): `cpu_requester_0`
2. **Port 0x253B write**: `cpu_requester_1` --- uses `nr_register` as the target
3. **ULA+ write** (port 0xFF3B with ULA+ mode "00"): `cpu_requester_2` --- writes NR 0xFF
4. **Copper**: Highest priority, can preempt CPU

Arbitration: `nr_wr_en = copper_req or cpu_req`. Copper always wins.

## Test Case Catalog

### 1. Register Selection and Access

| Test | Scenario | Expected |
|------|----------|----------|
| SEL-01 | Write 0x243B = 0x15, read 0x243B | Returns 0x15 |
| SEL-02 | Reset, read 0x243B | Returns 0x24 (protection default) |
| SEL-03 | Write 0x243B = 0x00, write 0x253B = 0x42, read NR 0x00 | Machine ID unaffected (read-only) |
| SEL-04 | Write 0x243B = 0x7F, write 0x253B = 0xAB, read NR 0x7F | Returns 0xAB (user register) |
| SEL-05 | NEXTREG ED 91 instruction | Writes correct register without changing nr_register |

### 2. Read-Only Registers

From the VHDL read dispatch, these registers return hardware-defined values
that cannot be changed by writing:

| Register | Name | Read value (from VHDL) |
|----------|------|----------------------|
| 0x00 | Machine ID | `g_machine_id` (generic) |
| 0x01 | Core version | `g_version` (generic) |
| 0x0E | Core sub-version | `g_sub_version` (generic) |
| 0x0F | Board issue | `g_board_issue` (generic, lower 4 bits) |
| 0x1E | Active video line MSB | `cvc(8)` (1 bit, right-justified) |
| 0x1F | Active video line LSB | `cvc(7 downto 0)` |

| Test | Scenario | Expected |
|------|----------|----------|
| RO-01 | Read NR 0x00 | Machine ID constant (see MID-01 for jnext value + deviation rationale) |
| MID-01 | Read NR 0x00 after reset | `0x08` (HWID_EMULATORS). **Deliberate deviation from VHDL** (`g_machine_id = X"0A"` in `zxnext_top_issue{2,4,5}.vhd:35`): jnext self-identifies as an emulator per TBBlue firmware convention so NextZXOS takes its emulator-aware boot paths. Reporting 0x0A makes NextZXOS divert into the FPGA-flash/Configuration flow, which fails for emulator-mounted SD images. Verified in `test/nextreg/nextreg_integration_test.cpp`. |
| RO-02 | Write NR 0x00, read back | Value unchanged |
| RO-03 | Read NR 0x01 | Core version constant |
| RO-04 | Read NR 0x0E | Sub-version constant |
| RO-05 | Read NR 0x0F | Board issue (lower nibble) |
| RO-06 | Read NR 0x1E/0x1F | Current video line |

### 3. Reset Defaults

From `zxnext.vhd` lines 4926-5100 (reset block). Key registers and their
reset values:

| Register | Reset Value | Bits | Notes |
|----------|-------------|------|-------|
| 0x05 | see below | joy0, joy1 | Not explicitly reset in main block |
| 0x06 | hotkeys enabled | bits 7,5 = 1 | CPU speed + 50/60 hotkeys on |
| 0x08 | 0x00 | all bits 0 | Contention on, no DAC, issue 3 kbd |
| 0x09 | sprite_tie=0 | bit 4 = 0 | No sprite tying |
| 0x0B | 0x01 | iomode_en=0, iomode_0=1 | I/O mode off |
| 0x12 | 0x08 | layer2 bank = 8 | Active L2 bank |
| 0x13 | 0x0B | shadow bank = 11 | Shadow L2 bank |
| 0x14 | 0xE3 | global transparent | Magenta (RRRGGGBB=E3) |
| 0x15 | 0x00 | all features off | No sprites, no lores, SLU priority 000 |
| 0x16 | 0x00 | L2 scroll X | 0 |
| 0x17 | 0x00 | L2 scroll Y | 0 |
| 0x18 clip | x1=0, x2=FF, y1=0, y2=BF | L2 clip | Full screen |
| 0x19 clip | x1=0, x2=FF, y1=0, y2=BF | Sprite clip | Full screen |
| 0x1A clip | x1=0, x2=FF, y1=0, y2=BF | ULA clip | Full screen |
| 0x1B clip | x1=0, x2=9F, y1=0, y2=FF | Tilemap clip | 320 wide, 256 tall |
| 0x22 | 0x00 | line int disabled | Line interrupt off |
| 0x42 | 0x07 | ULANext format | Default ink mask |
| 0x43 | 0x00 | palette control | All off |
| 0x4A | 0xE3 | fallback RGB | Magenta |
| 0x4B | 0xE3 | sprite transparent idx | Magenta index |
| 0x4C | 0x0F | tilemap transparent | Index 15 |
| 0x50 | 0xFF | MMU0 | ROM |
| 0x51 | 0xFF | MMU1 | ROM |
| 0x52 | 0x0A | MMU2 | Bank 5 page 0 |
| 0x53 | 0x0B | MMU3 | Bank 5 page 1 |
| 0x54 | 0x04 | MMU4 | Bank 2 page 0 |
| 0x55 | 0x05 | MMU5 | Bank 2 page 1 |
| 0x56 | 0x00 | MMU6 | Bank 0 page 0 |
| 0x57 | 0x01 | MMU7 | Bank 0 page 1 |
| 0x62 | mode="00" | Copper | Stopped |
| 0x68 | ula_en=1, rest=0 | ULA control | ULA enabled |
| 0x6B | 0x00 | Tilemap | Disabled |
| 0x70 | 0x00 | L2 resolution | 256x192, offset 0 |
| 0x82-85 | 0xFF | Internal port enables | All enabled |
| 0x86-89 | 0xFF | Bus port enables | All enabled |
| 0xC0 | 0x00 | IM2 vector/mode | All off |

| Test | Scenario | Expected |
|------|----------|----------|
| RST-01 | After reset, read NR 0x14 | 0xE3 |
| RST-02 | After reset, read NR 0x15 | 0x00 |
| RST-03 | After reset, read NR 0x4A | 0xE3 |
| RST-04 | After reset, read NR 0x42 | 0x07 |
| RST-05 | After reset, read NR 0x50-0x57 | MMU defaults |
| RST-06 | After reset, read NR 0x68 | 0x00 (bit 7 = NOT ula_en, so 0) |
| RST-07 | After reset, read NR 0x0B | 0x01 |
| RST-08 | After reset, read NR 0x82-0x85 | 0xFF |
| RST-09 | After reset, read NR 0x1B clip | x1=0, x2=0x9F, y1=0, y2=0xFF |

### 4. Register Read/Write Round-Trip

Test that writing a value and reading it back returns the expected encoding.
Some registers have fields that pack differently for read vs write.

| Test | Register | Write | Read Expected | Notes |
|------|----------|-------|---------------|-------|
| RW-01 | 0x07 | speed=3 | Bits 1:0 = 11, bits 5:4 = actual speed | CPU speed |
| RW-02 | 0x08 | 0xFF | Bit 7 = NOT port_7ffd_locked | Read differs from write |
| RW-03 | 0x12 | 0x10 | 0x10 | L2 active bank |
| RW-04 | 0x14 | 0x55 | 0x55 | Global transparent |
| RW-05 | 0x15 | 0x15 | 0x15 | Layer priority = 101, sprite en |
| RW-06 | 0x16 | 0xAA | 0xAA | L2 scroll X |
| RW-07 | 0x42 | 0xFF | 0xFF | ULANext format |
| RW-08 | 0x43 | 0x55 | 0x55 | Palette control |
| RW-09 | 0x4A | 0x42 | 0x42 | Fallback RGB |
| RW-10 | 0x50-57 | various | same | MMU pages |
| RW-11 | 0x7F | 0xAB | 0xAB | User register |
| RW-12 | 0x6B | 0x81 | 0x81 | Tilemap control |

### 5. Clip Window Cycling Index

Clip window registers (0x18, 0x19, 0x1A, 0x1B) use a cycling 2-bit index
that advances on each write. NR 0x1C can reset the indices.

From `zxnext.vhd` lines 5242-5290:

| Test | Scenario | Expected |
|------|----------|----------|
| CLIP-01 | Write NR 0x18 four times: 10,20,30,40 | x1=10, x2=20, y1=30, y2=40 |
| CLIP-02 | Write NR 0x18 five times | Index wraps, x1 overwritten |
| CLIP-03 | Write NR 0x1C bit 0 = 1 | L2 clip index resets to 0 |
| CLIP-04 | Write NR 0x1C bit 1 = 1 | Sprite clip index resets to 0 |
| CLIP-05 | Write NR 0x1C bit 2 = 1 | ULA clip index resets to 0 |
| CLIP-06 | Write NR 0x1C bit 3 = 1 | TM clip index resets to 0 |
| CLIP-07 | Read NR 0x1C | Returns all four 2-bit indices packed |
| CLIP-08 | Read NR 0x18 returns coord at current idx | Combinatorial mux over L2 clip coords — read does NOT advance idx (backlog: add read_handler for 0x18) |
| CLIP-09 | Read NR 0x1B twice with no intervening write | Both reads return same value (read does NOT advance idx — VHDL-faithful invariant) |
| CLIP-10 | Write NR 0x1B once, read NR 0x1C | bits 7:6 = 01 (tm idx advanced to 1) |

### 6. MMU Registers (0x50-0x57)

From `zxnext.vhd` lines 4607-4700:

| Test | Scenario | Expected |
|------|----------|----------|
| MMU-01 | Reset defaults | 0xFF,0xFF,0x0A,0x0B,0x04,0x05,0x00,0x01 |
| MMU-02 | Write NR 0x52 = 0x20, read back | 0x20 |
| MMU-03 | Write port 0x7FFD, check MMU6/7 | Updated from 7FFD bank field |
| MMU-04 | NextREG write overrides port write | Last writer wins |
| N8E-RAM-PRESERVE-0 | NR 0x56=0x20 override, then NR 0x8E=0x00 (bit 3 = 0) | MMU6 stays 0x20 — VHDL:3814 drives `port_memory_ram_change_dly='0'`, :4677 skips MMU6/7 update |
| N8E-RAM-REBUILD-1  | port_7ffd=0x03, NR 0x56=0x20 override, then NR 0x8E=0x08 (bit 3 = 1, bits 6:4 = 000) | MMU6 becomes 0x00 — 7FFD(2:0) forced to 0 by NR 0x8E bit 3 branch, :4677 rebuild runs and clobbers override |

### 7. Machine Config Registers

NR 0x03 has special behaviour (lines 5121-5151):

| Test | Scenario | Expected |
|------|----------|----------|
| CFG-01 | Write NR 0x03 bits 6:4 for timing | Machine timing changes |
| CFG-02 | Write NR 0x03 bit 3 toggles dt_lock | Lock toggled via XOR |
| CFG-03 | Write NR 0x03 bits 2:0 = 111 | Config mode entered |
| CFG-04 | Write NR 0x03 bits 2:0 = 001-100 | Machine type set, config mode exited |
| CFG-05 | Machine type only writable in config mode | Protected |
| CFG-06 | Write NR 0x03 bits 2:0 = 000 | No change to config_mode (no-op) |
| CFG-07 | Power-on / reset default | `nr_03_config_mode = 1` (`zxnext.vhd:1102`) |

### 8. Palette Registers (0x40-0x44)

From `zxnext.vhd` lines 4918-4920 and read dispatch:

| Test | Scenario | Expected |
|------|----------|----------|
| PAL-01 | Write NR 0x40 = 0x10 (palette index) | Palette index set |
| PAL-02 | Write NR 0x41 (8-bit colour) | Palette entry written |
| PAL-03 | Write NR 0x44 twice (9-bit colour) | sub_idx toggles, full value written |
| PAL-04 | Read NR 0x41 | Returns palette dat bits 8:1 |
| PAL-05 | Read NR 0x44 | Returns priority bits + LSB |
| PAL-06 | Auto-increment disabled (NR 0x43 bit 7) | Index does not advance |

### 9. Port Enable Registers (0x82-0x89)

From `zxnext.vhd` lines 2392-2442 and 5052-5068:

Internal port enables (0x82-0x85) control which internal peripherals respond.
Bus port enables (0x86-0x89) control which respond on expansion bus.
Effective enable = internal AND bus (when bus is active).

| Test | Scenario | Expected |
|------|----------|----------|
| PE-01 | Write NR 0x82 = 0x00 | All peripherals in group disabled |
| PE-02 | Read NR 0x82 after write | Returns written value |
| PE-03 | Disable joystick port (bit 6) | Port 0x1F not decoded |
| PE-04 | Reset with reset_type=1 | Internal ports reset to 0xFF |
| PE-05 | Reset with bus reset_type=0 | Bus ports reset to 0xFF |

### 10. Copper Arbitration

From `zxnext.vhd` lines 4706-4777:

| Test | Scenario | Expected |
|------|----------|----------|
| COP-01 | CPU write NR 0x15 | Value written |
| COP-02 | Copper write NR 0x15 simultaneously | Copper wins |
| COP-03 | CPU write while copper active | CPU waits |
| COP-04 | Copper register limited to 0x7F | MSB of copper reg forced to 0 |

## Test Count Summary

| Category | Tests |
|----------|-------|
| Register selection/access | ~5 |
| Read-only registers | ~6 |
| Reset defaults | ~9 |
| Read/write round-trip | ~12 |
| Clip window cycling | ~8 |
| MMU registers | ~4 |
| Machine config | ~5 |
| Palette registers | ~6 |
| Port enable registers | ~5 |
| Copper arbitration | ~4 |
| **Total** | **~64** |
