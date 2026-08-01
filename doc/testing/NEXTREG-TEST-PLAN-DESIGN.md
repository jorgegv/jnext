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

Measured on main 2026-04-20 post-Task-3 NextREG Phase 2 Wave 2 merges:

- **66 plan rows total** (CLIP-09/10 added 2026-04-20 Phase-2-E).
- **Bare test** (`test/nextreg/nextreg_test.cpp`): **21 pass / 0 fail / 0 skip** — FULLY GREEN. Phase 1 (2026-04-20) re-homed 32 bare skip()s to source comments pointing at the covering test. On 2026-04-21 the remaining two skips (COP-02 / COP-03, cycle-accurate CPU+Copper `nr_wr_*` bus arbitration per VHDL zxnext.vhd:4706-4777) were converted to WONT comments — a refinement of category G in `feedback_unobservable_audit_rule.md`: intentional behavioural simplification with documented reason (jnext serialises both write paths at the C++ call level; both writes land but without cycle-level overlap; no Copper demo or NextZXOS boot path depends on the exact ordering; un-skipping would need cycle-granularity tick API + dual-requester injection for zero user-visible payoff).
- **Integration test** (`test/nextreg/nextreg_integration_test.cpp`): **69 pass / 0 fail / 0 skip** out of 69 rows — FULLY GREEN. Six features merged across two waves:
  - **Wave 1 Feature A** (commit 0dc128e, merged in 504e646): NR 0x00 RO read_handler returning 0x08 (HWID_EMULATORS) via `src/core/emulator.cpp`. Un-skipped RO-02 + SEL-03. VHDL zxnext.vhd:5884-5885.
  - **Wave 1 Feature B** (commit 2cdc1f3, merged in 77a7106): reset seed defaults regs_[0x0E]=0x03 + regs_[0x89]=0x8F via `src/port/nextreg.cpp`. Un-skipped RO-04 + PE-05. VHDL zxnext_top_issue2.vhd:38 + zxnext.vhd:1234-1235, 6149-6150.
  - **Wave 1 Feature C** (commit cc51018, merged in f6cb811): NR 0x18 read_handler (pure combinatorial mux, no idx side-effect per VHDL zxnext.vhd:5947-5953) + public Layer2 `clip_x1/x2/y1/y2()` getters via `src/core/emulator.cpp` + `src/video/layer2.h`. Un-skipped CLIP-01/02/03/08.
  - **Wave 2 Feature D** (commit e6e2a1a): NR 0x07 packed read (VHDL-spec bits[5:4]=actual & bits[1:0]=requested per zxnext.vhd:5902-5903) + NR 0x82 bit 6 gate on port 0x1F Kempston 1 handler (VHDL:2392-2407). Un-skipped RW-01 + PE-03.
  - **Wave 2 Feature E** (commit 28921ce): full NR 0x03 state machine — machine_timing + dt_lock + machine_type — with save/load round-trip + VHDL-spec reset defaults (VHDL:1099-1103, 5124-5151, 5894). Un-skipped CFG-01 + CFG-02.
  - **Wave 2 Feature F** (commit 1fd1f36): SpriteEngine clip_x1/x2/y1/y2() public getters + Emulator::ula() accessor (Ula class already had clip_* getters). Un-skipped CLIP-04 + CLIP-05.
- **Matrix-tooling gap:** `test/refresh-traceability-matrix.pl` scans the one `nextreg_test.cpp` file per subsystem; Phase 1 re-homed skip()s show as `missing` (43 rows) and Wave 1/2 un-skips in integration don't flip the matrix aggregate (still 19 pass / 2 skip / 43 miss for NextREG). The covering test location is captured as a source comment on each re-homed row; a proper fix lives in [doc/design/REQUIREMENTS-DATABASE.md](../design/REQUIREMENTS-DATABASE.md) (future work).
- **Remaining known gap (nit)**: NR 0x03 read bit 7 returns 0 — jnext does not model `nr_palette_sub_idx`. Acceptable until palette-subindex work lands (inert at current software boundaries).
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
| SEL-05 | NEXTREG ED 91 instruction | Writes correct register without changing nr_register (today defers to fuse_z80_test/z80n_test, coverage unverified — split into SEL-05a/05b below) |
| SEL-05a | Pre-select NR 0x7F via 0x243B; execute Z80N `NEXTREG 0x54, 0x04` (ED 91 54 04); read 0x253B without re-selecting | NR 0x7F returned (selection preserved). VHDL `zxnext.vhd:4739-4744` injects `(reg, val)` directly via `cpu_requester_0` and never writes `nr_register` (:4592-4603). **IMPLEMENTED** as `nextreg_integration_test` Z80N-SEL-01 (was a skip; the defect it described was GH #54 and is fixed) |
| SEL-05b | Same setup with `NEXTREG 0x54,A` (ED 92); after it, write 0x253B ← 0x5C (raw data port) | NR 0x7F receives 0x5C (selection still pointed at 0x7F). **IMPLEMENTED** as `nextreg_integration_test` Z80N-SEL-02 |
| SEL-05c | Execute `NEXTREG 0x7E, 0x3C`; read NR 0x7E | NR 0x7E == 0x3C — the opcode still writes the register named by its own operand (`cpu_requester_reg <= Z80N_data_s(15 downto 8)`). Guards SEL-05a/05b against a no-op "fix". **IMPLEMENTED** as `nextreg_integration_test` Z80N-SEL-03 |

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
| MID-01 | Read NR 0x00 after reset | `0x0A` — VHDL-faithful (`g_machine_id = X"0A"` in `zxnext_top_issue{2,4,5}.vhd:35`). NextZXOS-boot fix 2026-07-09: the former deliberate `0x08` (HWID_EMULATORS) deviation was reverted — the zx_go reference emulator documents `0x08` breaking NextZXOS's ROM1 machine-ID check and boots end-to-end with `0x0A`; jnext now does too. Verified in `test/nextreg/nextreg_integration_test.cpp`. |
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
| NREG-RST-01 | After reset, read NR 0x14 | 0xE3 |
| NREG-RST-02 | After reset, read NR 0x15 | 0x00 |
| NREG-RST-03 | After reset, read NR 0x4A | 0xE3 |
| NREG-RST-04 | After reset, read NR 0x42 | 0x07 |
| NREG-RST-05 | After reset, read NR 0x50-0x57 | MMU defaults |
| NREG-RST-06 | After reset, read NR 0x68 | 0x00 (bit 7 = NOT ula_en, so 0) |
| NREG-RST-07 | After reset, read NR 0x0B | 0x01 |
| NREG-RST-08 | After reset, read NR 0x82-0x85 | 0xFF |
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
| NR-MMU-02 | Write NR 0x52 = 0x20, read back | 0x20 |
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
| PE-06 | Read NR 0x82 after a write, full VHDL packing | Write NR 0x82 ← 0x55; read NR 0x82 | Returned byte equals VHDL composition (`zxnext.vhd:5508-5522`) — internal-port-enable bits AND with reset_type-derived defaults; NOT raw shadow. skip — jnext stores raw write byte in `regs_[]` (see G154) |
| PE-07 | Read NR 0x86 (bus-port-enable, no read_handler today) | Write NR 0x86 ← 0x33; read NR 0x86 | Returned byte equals VHDL packing at `zxnext.vhd:5061-5067`. skip — jnext returns raw `regs_[0x86]` because no read_handler is installed (see G154) |
| PE-08 | Read NR 0x89 inverted-reset semantics | After power-on with `reset_type=0` (NR 0x02 b0=0), read NR 0x89 | NR 0x89 bit 7 is INVERTED — VHDL `zxnext.vhd:6138, 6150` clears NR 0x89 to 0xFF on `reset_type=0`. skip — jnext returns raw stored byte (0x00 default) (see G154) |
| PE-09 | Read NR 0x80 / 0x88 not initialised | Power-on, read NR 0x80 / 0x88 | Returned byte matches the VHDL reset-table value, not zero. skip — jnext skips initialisation for NRs without explicit handlers (see G154) |

### 10. Copper Arbitration

From `zxnext.vhd` lines 4706-4777:

| Test | Scenario | Expected |
|------|----------|----------|
| COP-01 | CPU write NR 0x15 | Value written |
| COP-02 | Copper write NR 0x15 simultaneously | Copper wins |
| COP-03 | CPU write while copper active | CPU waits |
| COP-04 | Copper register limited to 0x7F | MSB of copper reg forced to 0 |

### 11. Write-Only Register Read Behaviour (G149)

VHDL `zxnext.vhd:5878-6289`: read-mux falls through to `(others => '0')`
for NRs without read entries. jnext `src/port/nextreg.cpp:101-110`
returns `regs_[reg]` instead, leaking the last-written byte for
write-only NRs (0x04, 0x29-0x2B, 0x35-0x39, 0x60, 0x63, 0x75-0x79).
Distinct from G56 (composed-read divergence on NRs *with* read entries).

| Test  | Scenario                                                    | Expected                                                                                  |
|-------|-------------------------------------------------------------|-------------------------------------------------------------------------------------------|
| WO-01 | Write NR 0x04 ← 0xA5; read NR 0x04 via 0x243B/0x253B        | Returns 0x00 — `zxnext.vhd:5878-6289` read-mux falls through to `(others => '0')`. skip — jnext leaks last-written byte (see G149) |
| WO-02 | Write NR 0x29 ← 0x55; read NR 0x29                          | Returns 0x00; sprite stream NRs (0x29/0x2A/0x2B) are write-only per VHDL. skip — leaks last-written byte (see G149) |
| WO-03 | Write NR 0x60 ← 0x42; read NR 0x60                          | Returns 0x00; copper data port is write-only per VHDL `:5878-6289`. skip — leaks last-written byte (see G149) |
| WO-04 | Write NR 0x35 ← 0x33; read NR 0x35                          | Returns 0x00; sprite-attribute mirror NR is write-only per VHDL. skip — leaks last-written byte (see G149) |

## Test Count Summary

| Category | Tests |
|----------|-------|
| Register selection/access | ~7 (+SEL-05a/05b G151) |
| Read-only registers | ~6 |
| Reset defaults | ~9 |
| Read/write round-trip | ~12 |
| Clip window cycling | ~8 |
| MMU registers | ~4 |
| Machine config | ~5 |
| Palette registers | ~6 |
| Port enable registers | ~9 (+PE-06..09 G154) |
| Copper arbitration | ~4 |
| Write-only read behaviour | 4 (WO-01..04 G149) |
| **Total** | **~74** |

## Task 58 append (2026-07-14) — NR 0x05 bits 2/0 readback is frame-edge-latched

The NR 0x05 read mux (VHDL `zxnext.vhd:5897`) composes:

    port_253b_dat <= nr_05_joy0(1:0)              -- bits 7:6 (pending)
                   & nr_05_joy1(1:0)              -- bits 5:4 (pending)
                   & nr_05_joy0(2)                -- bit  3  (pending)
                   & eff_nr_05_5060               -- bit  2  (EFFECTIVE)
                   & nr_05_joy1(2)                -- bit  1  (pending)
                   & eff_nr_05_scandouble_en;     -- bit  0  (EFFECTIVE)

Bits 2 and 0 are the `eff_` copies, latched from the pending FFs only
at `video_frame_sync = '1'` (`zxnext.vhd:6696-6703`): between a write
and the next frame edge, real hardware reads the OLD values. The joy
bits have no `eff_` copy and follow the write immediately.

**Oracle correction.** Several existing rows asserted the immediate
write→read round-trip for bits 2/0 — that encoded jnext's pending-
readback behaviour (the write-through cache), i.e. they were locked to
the WRONG oracle. Task 58 makes the emulator read return the effective
values (`VideoTiming::refresh_60hz()` for bit 2; a frame-edge-latched
`eff_nr_05_scandouble_en_` for bit 0) and corrects the rows to sample
after a frame edge. Pentagon gating is unchanged and lives entirely in
the PENDING path (`zxnext.vhd:5835-5836` forces the pending 5060 FF to
0 continuously; the frame latch then propagates it) — the former
read-time Pentagon mask is removed as not VHDL-faithful.

Rows (in `nextreg_integration_test`):

| ID | Change | VHDL |
|----|--------|------|
| T58-NR05-EFF-01 | NEW discriminative row: write 0x05 → read 0x00 BEFORE the frame edge (old eff), 0x05 after | zxnext.vhd:5897, :6696-6703 |
| G56-CR-NR05-02/03/04 | corrected: round-trip sampled after `run_frame()` (frame edge latches eff bits) | zxnext.vhd:5897, :6696-6703 |
| TC-NR05-PRESERVE | corrected: frame edge before sampling so eff == pending | zxnext.vhd:6696-6703 |
| TC-NR05-PENTAGON | reworked: real NR 0x03 write path + frame edges; bit 2 reads 0 after Pentagon entry **+ frame edge** (was: read-time mask, wrong oracle) | zxnext.vhd:5835-5836, :6697-6700, :5897 |
| V13-NMP-01 | corrected: frame edges added so each sample reads the latched eff value | zxnext.vhd:5835-5836, :5897, :6696-6703 |

`input_test` FNK-02 / FNK-03 receive the analogous correction (see the
INPUT plan's Task 58 append).

Mutation evidence: reverting the read handler to pending-cache bits
turns T58-NR05-EFF-01 (and input FNK-02/03) RED; restoring turns them
green.
