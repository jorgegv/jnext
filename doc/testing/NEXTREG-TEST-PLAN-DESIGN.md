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

> **2026-09-24 (GH #201).** The 24 rows this plan declared that no suite
> asserted have been dispositioned: 22 RETIRED (struck in place, each naming
> the live row that covers it — or, for COP-02/03, the standing WONT), 2
> implemented (PE-04, NR-MMU-04). See the [GH #201 append](#gh-201-append-2026-09-24--the-24-plan-only-rows-dispositioned)
> at the foot of this document for the full disposition table. The measured
> numbers below are from 2026-04-20 and are historical.

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
> **Renamed 2026-08-01 (GH #196 phase 4.2): `SEL-05` -> `NR-SEL-05`.** The bare
> name collided with `uart_test`'s `SEL-05` (UART channel select read), which
> asserts it; this row is plan-doc-only. `SEL-01..04` keep their bare names —
> `nextreg_test` asserts them and nothing else claims them.

| Test | Scenario | Expected |
|------|----------|----------|
| SEL-01 | Write 0x243B = 0x15, read 0x243B | Returns 0x15 |
| SEL-02 | Reset, read 0x243B | Returns 0x24 (protection default) |
| SEL-03 | Write 0x243B = 0x00, write 0x253B = 0x42, read NR 0x00 | Machine ID unaffected (read-only) |
| SEL-04 | Write 0x243B = 0x7F, write 0x253B = 0xAB, read NR 0x7F | Returns 0xAB (user register) |
| ~~NR-SEL-05~~ | ~~NEXTREG ED 91 instruction~~ | **RETIRED 2026-09-24 (GH #201)** — this row was never a test of its own: it was the umbrella row that the plan itself split into SEL-05a/05b/05c, each of which is now live under a different ID (see the three rows below). Keeping it emitted a fourth `missing` row for a claim that is asserted three times over. No `check()` row exists. |
| ~~SEL-05a~~ | ~~Pre-select NR 0x7F via 0x243B; execute Z80N `NEXTREG 0x54, 0x04` (ED 91 54 04); read 0x253B without re-selecting~~ | **RETIRED 2026-09-24 (GH #201)** — asserted LIVE as `Z80N-SEL-01` in `test/nextreg/nextreg_integration_test.cpp` (group `Z80N-NEXTREG-Select`): pre-selects NR 0x7F, runs `ED 91 54 04` from RAM through `execute_single_instruction()`, then checks both `NextReg::selected() == 0x7F` and `IN (0x253B) == 0xAB`. Same VHDL oracle (`zxnext.vhd:4739-4744` vs :4592-4603). Renamed, not lost. No `check("SEL-05a")` row exists. |
| ~~SEL-05b~~ | ~~Same setup with `NEXTREG 0x54,A` (ED 92); after it, write 0x253B ← 0x5C (raw data port)~~ | **RETIRED 2026-09-24 (GH #201)** — asserted LIVE as `Z80N-SEL-02` in `test/nextreg/nextreg_integration_test.cpp`, same stimulus (`ED 92 54` with A=0x04, then `OUT (0x253B),0x5C`) and same oracle (`zxnext.vhd:4739-4744`). No `check("SEL-05b")` row exists. |
| ~~SEL-05c~~ | ~~Execute `NEXTREG 0x7E, 0x3C`; read NR 0x7E~~ | **RETIRED 2026-09-24 (GH #201)** — asserted LIVE as `Z80N-SEL-03` in `test/nextreg/nextreg_integration_test.cpp`; it is the guard that keeps Z80N-SEL-01/02 from being satisfied by a no-op opcode (`cpu_requester_reg <= Z80N_data_s(15 downto 8)`). No `check("SEL-05c")` row exists. |

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
> **Renamed 2026-08-01 (GH #196 phase 4.2): `MMU-01`/`MMU-03`/`MMU-04` ->
> `NR-MMU-*`.** The bare names collided with `mmu_test`'s `MMU-*`, which
> asserts them; these three are plan-doc-only. `NR-MMU-02` already carried the
> prefix, so this completes a convention rather than inventing one.

| Test | Scenario | Expected |
|------|----------|----------|
| ~~NR-MMU-01~~ | ~~Reset defaults~~ | **RETIRED 2026-09-24 (GH #201)** — asserted LIVE as `NREG-RST-05` in `test/nextreg/nextreg_integration_test.cpp` (group `Reset-Integration`), which reads NR 0x50-0x57 through the port path after a power-on reset and compares against the identical VHDL default vector `0xFF,0xFF,0x0A,0x0B,0x04,0x05,0x00,0x01` with the same citation (`zxnext.vhd:4610-4618`). Reset defaults are subsystem-owned (the `Mmu` mirror), so the bare tier could never assert them — `nextreg_test.cpp` has recorded the re-home as a source comment since the Phase 1 re-home. No `check("NR-MMU-01")` row exists. |
| NR-MMU-02 | Write NR 0x52 = 0x20, read back | 0x20 |
| ~~NR-MMU-03~~ | ~~Write port 0x7FFD, check MMU6/7~~ | **RETIRED 2026-09-24 (GH #201)** — asserted LIVE as `P7F-01..P7F-08` in `test/mmu/mmu_test.cpp` (group `Cat3 port 0x7FFD`), which drives `map_128k_bank()` for all eight banks and checks `MMU6 == 2*B` / `MMU7 == 2*B+1` against the same VHDL rebuild arm. The port 0x7FFD decoder is `Mmu`-owned; bare `NextReg` has none. No `check("NR-MMU-03")` row exists. |
| NR-MMU-04 | NextREG write overrides port write | Last writer wins — MMU0..7 are one clocked register with three mutually exclusive arms (`reset` / `port_memory_change_dly` / `nr_mmu_we`, `zxnext.vhd:4607-4699`), so whichever arm fires on the later clock edge survives. **IMPLEMENTED 2026-09-24 (GH #201)** as `check("NR-MMU-04")` in `test/nextreg/nextreg_integration_test.cpp` (group `NR-MMU-Arbitration`): port 0x7FFD←bank 3 → MMU6/7 = 0x06/0x07; NR 0x56←0x20 → MMU6 = 0x20 with MMU7 untouched; port 0x7FFD←bank 1 → MMU6/7 = 0x02/0x03. The middle step is the direction `N8E-RAM-REBUILD-1` does not cover. |
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
| PE-04 | Reset with reset_type=1 | Internal ports reset to 0xFF (NR 0x85 reads 0x8F — its enable field is 4 bits and bits 6:4 are hard "000" in the read mux, `zxnext.vhd:6138`). **IMPLEMENTED 2026-09-24 (GH #201)** as `check("PE-04")` in `test/nextreg/nextreg_test.cpp` (group `Port-Enable`), asserting BOTH axes of the gate at `zxnext.vhd:5052-5058`: with NR 0x85 bit 7 = 1 the group reloads to 0xFF/0xFF/0xFF/0x8F, and with bit 7 = 0 it survives the reset verbatim. The negative axis is the discriminative one — `NREG-RST-08` reads the group only after a power-on reset, where `nr_85_internal_port_reset_type` is already '1' from its `:1230` initialiser, so an unconditional reload still satisfies it. |
| PE-05 | Reset with bus reset_type=0 | Bus ports reset to 0xFF |
| ~~PE-06~~ | ~~Read NR 0x82 after a write, full VHDL packing~~ | **RETIRED 2026-09-24 (GH #201)** — two reasons. (a) The stated oracle was a PLAN BUG: `zxnext.vhd:5499` stores all 8 written bits into `nr_82_internal_port_enable` and `:6128-6129` returns all 8 verbatim, so the VHDL read IS the raw shadow — there is no "AND with reset_type-derived defaults" composition on this register. (b) The corrected claim is asserted LIVE as `PE-INT-82` in `test/nextreg/nextreg_integration_test.cpp` (write 0xA5, read 0xA5, no pack mask), whose header block records the corrected oracle with the same citations. No `check("PE-06")` row exists. |
| ~~PE-07~~ | ~~Read NR 0x86 (bus-port-enable, no read_handler today)~~ | **RETIRED 2026-09-24 (GH #201)** — asserted LIVE as `PE-INT-86` in `test/nextreg/nextreg_integration_test.cpp`: observes the reset default 0xFF (`zxnext.vhd:1231`) and the write/read round-trip (`:5512` write 8 bits, `:6140-6141` read 8 bits). The "no read_handler today" skip reason is stale — NR 0x86 needs none, because the VHDL read is an identity on the stored byte. `PE-INT-87` adds the same shape for NR 0x87. No `check("PE-07")` row exists. |
| ~~PE-08~~ | ~~Read NR 0x89 inverted-reset semantics~~ | **RETIRED 2026-09-24 (GH #201)** — split and asserted LIVE in `test/nextreg/nextreg_integration_test.cpp` across two rows: `PE-INT-89` pins the read PACKING (write 0xF7 → read 0x87, because `:5520-5522` routes bit 7 to `nr_89_bus_port_reset_type` and bits 3:0 to the enable field while discarding bits 6:4, and `:6149-6150` recomposes `reset_type & "000" & enable`), and `PE-05` pins the post-reset default 0x8F (`:1234-1235`). The inverted polarity the row names — the bus group reloads when `nr_89_bus_port_reset_type` is '0', the opposite of the `0x82-0x85` group — lives in `NextReg::reset()` and is documented there. No `check("PE-08")` row exists. |
| ~~PE-09~~ | ~~Read NR 0x80 / 0x88 not initialised~~ | **RETIRED 2026-09-24 (GH #201)** — asserted LIVE as `PE-INT-80-88` in `test/nextreg/nextreg_integration_test.cpp`: NR 0x80 reads 0x00 (`zxnext.vhd:360`) and NR 0x88 reads 0xFF (`:1233`) after a power-on reset. The "jnext skips initialisation" skip reason is stale — the G154 closure seeded both bytes in `NextReg::reset()`. No `check("PE-09")` row exists. |

### 10. Copper Arbitration

From `zxnext.vhd` lines 4706-4777:

| Test | Scenario | Expected |
|------|----------|----------|
| COP-01 | CPU write NR 0x15 | Value written |
| ~~COP-02~~ | ~~Copper write NR 0x15 simultaneously~~ | **RETIRED 2026-09-24 (GH #201) — WONT, not a gap.** jnext has no shared `nr_wr_*` bus: the CPU and Copper NextREG write paths are serialised at the C++ call level, so "the same 28 MHz cycle" is a state the emulator cannot enter and a row here could only order the two writes by hand and then assert that the later one won — which is NOT what `zxnext.vhd:4769,4775-4777` claims (there the Copper wins even when the CPU asked first on that cycle). Writing it anyway would be a row that passes for a reason unrelated to its claim. The decision was taken 2026-04-21 (recorded as a WONT comment in `test/nextreg/nextreg_test.cpp`, group `Copper-Arb`) and re-confirmed by Task 65 (2026-07-17) in `doc/design/EMULATOR-DESIGN-PLAN.md` — "Model cycle-accurate CPU/Copper NR write priority", resolved as option (a): priority stays a test-harness convention and a documented modelling limitation, because option (b) was gated on a cycle-accurate refactor the copper/beast 400% assessment ruled out as negative-payoff. Re-open only if concrete software reveals a divergence. No `check()` row exists. |
| ~~COP-03~~ | ~~CPU write while copper active~~ | **RETIRED 2026-09-24 (GH #201) — WONT, not a gap.** Same decision, same evidence as COP-02: this is the CPU-held-back half of the `cpu_req` guard at `zxnext.vhd:4769`, and jnext has no cycle in which a CPU write can be held. Both writes land; only the cycle-level overlap is absent. No `check()` row exists. |
| ~~COP-04~~ | ~~Copper register limited to 0x7F~~ | **RETIRED 2026-09-24 (GH #201)** — asserted LIVE as `ARB-04` ("Copper cannot address NR 0x80..0xFF") in `test/copper/copper_test.cpp`, which drives a Copper MOVE at a masked and an unmasked register index and checks NR 0x7F took the byte while NR 0xFF did not; `MOV-02` and `MOV-07` cover the positive side (a full 7-bit register index reaching NR 0x7F through MOVE). The mask lives in the `Copper` class, not in `NextReg`, which is why the bare NextREG tier could never assert it. No `check("COP-04")` row exists. |

### 11. Write-Only Register Read Behaviour (G149)

VHDL `zxnext.vhd:5878-6289`: read-mux falls through to `(others => '0')`
for NRs without read entries. jnext `src/port/nextreg.cpp:101-110`
returns `regs_[reg]` instead, leaking the last-written byte for
write-only NRs (0x04, 0x29-0x2B, 0x35-0x39, 0x60, 0x63, 0x75-0x79).
Distinct from G56 (composed-read divergence on NRs *with* read entries).

| Test  | Scenario                                                    | Expected                                                                                  |
|-------|-------------------------------------------------------------|-------------------------------------------------------------------------------------------|
| ~~WO-01~~ | ~~Write NR 0x04 ← 0xA5; read NR 0x04 via 0x243B/0x253B~~ | **RETIRED 2026-09-24 (GH #201)** — G149 is CLOSED and the row is asserted LIVE as `WO-INT-04` in `test/nextreg/nextreg_integration_test.cpp` (group `WO-Integration`): write 0xAA, read 0x00, same oracle (`zxnext.vhd:5878-6289` falls through to `(others => '0')`). The fix is per-register: the Emulator's NR 0x04 write_handler returns 0, so the canonicalised `regs_[0x04]` stores 0 — which only exists on a fully-wired `Emulator`, hence the integration tier. The skip reason above ("jnext leaks last-written byte") is stale. No `check("WO-01")` row exists. |
| ~~WO-02~~ | ~~Write NR 0x29 ← 0x55; read NR 0x29~~ | **RETIRED 2026-09-24 (GH #201)** — asserted LIVE as `WO-INT-29`; the sibling keymap NRs are covered too, by `WO-INT-2A` (dead register, write strobe commented out at `zxnext.vhd:4850`) and `WO-INT-2B` (`nr_2b_we` at `:6306-6307`). No `check("WO-02")` row exists. |
| ~~WO-03~~ | ~~Write NR 0x60 ← 0x42; read NR 0x60~~ | **RETIRED 2026-09-24 (GH #201)** — asserted LIVE as `WO-INT-60`; the other write-only Copper data port is covered by `WO-INT-63` (`nr_copper_we` at `zxnext.vhd:4887`, write side `:5433-5439`), both of which keep the write side-effect and pin only the read. No `check("WO-03")` row exists. |
| ~~WO-04~~ | ~~Write NR 0x35 ← 0x33; read NR 0x35~~ | **RETIRED 2026-09-24 (GH #201)** — asserted LIVE as `WO-INT-35`. `WO-INT-FF` extends the same class to NR 0xFF (ULA+ palette poke, `zxnext.vhd:4906`/`:4919`). No `check("WO-04")` row exists. |

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
| Port enable registers | ~9 (PE-06..09 RETIRED 2026-09-24 → `PE-INT-*`; PE-04 now live) |
| Copper arbitration | ~4 (COP-02/03 RETIRED as WONT, COP-04 RETIRED → `copper_test` ARB-04) |
| Write-only read behaviour | 4 — all RETIRED 2026-09-24 → `WO-INT-04/29/35/60` |
| **Total** | **~74** declared, of which 22 are RETIRED (see the GH #201 append) |

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

## GH #196 Phase 1.3 append (2026-08-01) — Extra-coverage table folded/dropped

The `## NextREG` section of `doc/testing/TRACEABILITY-MATRIX.md` carried a
17-row `### Extra coverage (not in plan)` table (4 columns, no `Status`) —
rows that escaped the normal main-table scheme, per the GH #192 lineage.
Disposition, one unit of the GH #196 Phase 1.3 plan:

- **`NREG-RST-10`, `NREG-RST-11`, `NREG-RST-12`** — stale duplicates.
  The companion `test/nextreg/nextreg_integration_test.cpp` table already
  carries these exact IDs at the exact same test file:line (281/291/301)
  with the CURRENT, correct description (verified against the live
  `check()` call text) — "NR 0x12 Layer 2 active bank reset = 0x08",
  "NR 0x4B sprite transparent index reset = 0xE3", "NR 0x4C tilemap
  transparent index reset = 0x0F" respectively. The extra-coverage
  table's own descriptions for these three IDs were stale/wrong (e.g.
  it described `NREG-RST-11` as "NR 0x68 ULA control", not NR 0x4B).
  Removed, no functional loss — the correct row already existed.
- **`NREG-RST-13`** — genuinely escaped row, folded into the companion
  table. It had no other row anywhere citing the same ID. Its own
  extra-coverage description was ALSO stale ("NR 0x82-0x85 internal
  port enables = 0xFF", duplicating `NREG-RST-08`'s topic) — the actual
  `check("NREG-RST-13", ...)` call at
  `test/nextreg/nextreg_integration_test.cpp:343` asserts the DivMMC
  automap entry-point registers NR 0xB8/0xB9/0xBA/0xBB reset to
  0x83/0x01/0x00/0xCD (`zxnext.vhd:5087-5090`; V17-NMP-01 fix). Folded
  in with the corrected description.
- **13 rows had no live test anywhere** (`grep -rn` over `test/nextreg/`
  returns zero hits for the ID string): `RST-14`, `RST-15`, `RST-16a`,
  `RST-16b`, `WH-01..04`, `EDGE-01..05`. Git history (`git log -S`)
  traces all 13 to commit `6a8094fb` ("rewrite in Phase 2 per-row
  idiom"), which dropped them without replacement when the suite
  adopted the current VHDL-citation-per-row discipline documented at
  the top of `test/nextreg/nextreg_test.cpp`. Per-row disposition:
  - `WH-01..04` and `EDGE-01`/`EDGE-05` tested the bare `NextReg`
    class's generic handler-registration mechanism (write/read handler
    dispatch, 256-register round-trip, multi-select "last wins") —
    implementation-detail tests with no VHDL citation, deliberately
    incompatible with the current "every row cites VHDL" idiom.
    Correctly dropped, not a coverage gap.
  - `EDGE-02` ("reset clears NR 0x7F to 0") and `EDGE-03` ("reset
    restores NR 0x00=0x0A") are actively **contradicted** by current,
    VHDL-faithful behaviour: NR 0x7F has no VHDL reset clause and
    SURVIVES reset (see the `nextreg_integration_test` "NR 0x7F
    survives reset" row), and the bare class deliberately resets NR
    0x00 to 0x08 (`HWID_EMULATORS`), not the VHDL 0x0A (see `MID-01`
    in the companion table, which covers the real 0x0A behaviour at
    the integration tier). Stale, not a live gap.
  - `RST-15` ("NR 0x4B sprite transparent = 0xE3") is fully covered
    today under `NREG-RST-11` in the companion table — same register,
    same expected value, different (current) ID.
  - `RST-14` ("NR 0x86-0x89 bus port enables = 0xFF") is superseded by
    the current, more nuanced `PE-05` (NR 0x89 reset = 0x8F, not 0xFF,
    reset-type dependent) and the still-open `PE-08` gap (bit-7
    inversion on `reset_type=0`) — the old blanket-0xFF framing across
    all four registers does not hold under closer VHDL reading.
  - `EDGE-04` ("write handler survives reset") and `RST-16a`/`RST-16b`
    (NR 0x16/0x17 L2 scroll-X/Y reset default = 0x00) are genuine gaps
    with no current equivalent — dropped as orphans, not folded (no
    live test exists to fold).
  All 13 dropped; none folded.

## Planned rows carried over from the traceability matrix (GH #196)

These rows were recorded only in `TRACEABILITY-MATRIX.md`, which is now a
generated artifact and can no longer hold a claim of its own. They are
planned and NOT implemented, so they are recorded here — the one place the
generator reads planned rows from — and the matrix emits them as `missing`,
which is what they are.

| ID | Description | VHDL file:line |
|----|-------------|----------------|
| ~~FT-D8-01~~ | ~~NR 0xD8 nr_d8_io_trap_fdc_en write/read-back~~ — **RETIRED 2026-09-24 (GH #201)**: asserted LIVE as `FT-INT-D8-01` in `test/nextreg/nextreg_integration_test.cpp`, group `FT-Integration` (write 1 → read 0x01, write 0 → read 0x00; the bits-7:1-are-zero shape of the read mux). The whole FT family was re-homed to the `FT-INT-*` names when the iotrap chain landed — `test/nextreg/nextreg_test.cpp` records the five-row mapping as a source comment — and the old plan IDs were left behind. | ~~zxnext.vhd:5639-5640, 6265-6266~~ |
| ~~FT-D8-02~~ | ~~NR 0xD8 enable=1 must allow strobe_iotrap to assert MF~~ — **RETIRED 2026-09-24 (GH #201)**: asserted LIVE as `FT-INT-D8-02` (NR 0xD8 b0 = 1 plus NR 0x06 b3 = 1, then a port 0x3FFD write, then `NmiSource::nmi_assert_mf()`). | ~~zxnext.vhd:2601-2602, 3835, 3837~~ |
| ~~FT-D9-01~~ | ~~NR 0xD9 nr_d9_iotrap_write captures CPU write byte~~ — **RETIRED 2026-09-24 (GH #201)**: asserted LIVE as the pair `FT-INT-D9-01a` (the firmware direct-write arm, `nr_d9_we` at `zxnext.vhd:4901` — note the `:5643` copy is a commented-out vestigial duplicate) and `FT-INT-D9-01b` (the port 0x3FFD capture arm, `:3892-3893`). `TC-IOTRAP-IDLE-GATE` adds the `nmi_accept_cause` gate on both. | ~~zxnext.vhd:3892-3893~~ |
| ~~FT-DA-01~~ | ~~NR 0xDA nr_da_iotrap_cause encoding 01/10/11~~ — **RETIRED 2026-09-24 (GH #201)**: asserted LIVE as the triple `FT-INT-DA-01a` (0x2FFD read → "01"), `FT-INT-DA-01b` (0x3FFD read → "10") and `FT-INT-DA-01c` (0x3FFD write → "11"), one per arm of `zxnext.vhd:3871-3878`. | ~~zxnext.vhd:3872-3877~~ |
| ~~FT-DA-02~~ | ~~NR 0xDA cause clears via NR 0x02 b4 write=0~~ — **RETIRED 2026-09-24 (GH #201)**: asserted LIVE as `FT-INT-DA-02` (cause ← "11" via a 0x3FFD write, then NR 0x02 ← 0x00, then cause reads 0x00). | ~~zxnext.vhd:3879-3880~~ |
| G56-CR-05 | NR 0x05 composed-read divergence | zxnext.vhd:5896-5897 |
| G56-CR-06 | NR 0x06 psg_mode source-of-truth | zxnext.vhd:5899-5900 |
| G56-CR-09 | NR 0x09 sprite_tie composed-read | zxnext.vhd:5908-5909 |
| G56-CR-0A | NR 0x0A divmmc_automap_en mirror | zxnext.vhd:5911-5912 |
| G56-CR-0B | NR 0x0B joystick composed-read | zxnext.vhd:5914-5915 |
| G56-CR-10 | NR 0x10 SPKEY_BUTTONS/coreid live composed-read (title says "video-timing cvc": mismatch — that is NR 0x11/NR 0x1E-1F, not this arm) | zxnext.vhd:5923-5924 |
| G56-CR-15 | NR 0x15 layer composed-read | zxnext.vhd:5938-5939 |
| G56-CR-22 | NR 0x22 bit 7 dynamic pulse_int_n | zxnext.vhd:5991-5992 |
| G56-CR-23 | NR 0x23 line-int compare ladder (register readback itself is a plain stored-value passthrough; the actual compare is zxula_timing.vhd:577) | zxnext.vhd:5994-5995 |
| G56-CR-34 | NR 0x34 sprite-attr index live counter | zxnext.vhd:6032-6033 |
| G56-CR-40 | NR 0x40 palette idx autoinc state | zxnext.vhd:6035-6036 |
| G56-CR-43 | NR 0x43 palette ctrl composed-read | zxnext.vhd:6044-6045 |
| G56-CR-4C | NR 0x4C bits 7:4 mask not propagated | zxnext.vhd:6056-6057 |
| G56-CR-68 | NR 0x68 b4 from port_ff3b_ulap_en | zxnext.vhd:6092-6093 |
| G56-CR-69 | NR 0x69 bits composed from port_ff | zxnext.vhd:6095-6096 |
| G56-CR-6A | NR 0x6A radastan/lores composed | zxnext.vhd:6098-6099 |
| G56-CR-6B | NR 0x6B b7 from nr_6b_tm_en | zxnext.vhd:6101-6102 |
| G56-CR-6C | NR 0x6C tilemap composed-read | zxnext.vhd:6104-6105 |
| G56-CR-6E | NR 0x6E bit 6 always 0 | zxnext.vhd:6107-6108 |
| G56-CR-6F | NR 0x6F bit 6 always 0 | zxnext.vhd:6110-6111 |
| G56-CR-70 | NR 0x70 bits 7:6 always 0 | zxnext.vhd:6113-6114 |
| G56-CR-71 | NR 0x71 bits 7:1 always 0 | zxnext.vhd:6116-6117 |
| G56-CR-80 | NR 0x80 expansion-bus dynamic state | zxnext.vhd:6122-6123 |
| G56-CR-81 | NR 0x81 b7 from i_BUS_ROMCS_n | zxnext.vhd:6125-6126 |

## Coverage notes (moved from the traceability matrix, GH #196)

The matrix is a generated artifact now and carries no prose of its own; it
links here instead. These notes were written alongside the rows they explain.

Created 2026-04-15 onwards (Phase 2 Wave 1 commit `0dc128e` and beyond) to host integration-tier rows from the NextREG plan that require the full `Emulator` fixture (subsystem wiring for reset defaults, MMU/Layer2/Sprite/Tilemap clip-window cycling, palette pipeline, NR 0x82-bit-6 port-1F gate, NR 0x07/0x08 read composition, NR 0x03 machine-config state, DMA IM2-delay composition, soft-reset semantics, NR 0x8E RAM-rebuild gate, Layer 2 bank routing). Runtime: `Total:  301  Passed:  301  Failed:    0  Skipped:    0`. The 74 rows listed below are only the ones recorded here; 37 more that the suite asserts are recorded in the parent `## NextREG` table above, and the rest are reported `unrecorded` on every run. Each row cross-references the bare-suite plan row when a re-home applies.


## GH #201 append (2026-09-24) — the 24 plan-only rows, dispositioned

`doc/testing/TRACEABILITY-MATRIX.md` reported 24 NextREG rows as `missing`:
rows this plan declares that no suite asserts. Twenty-two of them turned out
to be **bookkeeping**, not coverage: the work had been done and landed under a
different ID, and only the plan doc was never told. Two were real.

The bookkeeping cases cluster by era, and each cluster has the same shape —
a bare-tier row that could not stay bare, re-homed to the integration tier or
to the owning subsystem's suite, with the re-home recorded as a source comment
in `test/nextreg/nextreg_test.cpp` and nowhere else:

| Cluster | Plan IDs | Now asserted as | Suite |
|---------|----------|-----------------|-------|
| Z80N `NEXTREG` select latch | NR-SEL-05, SEL-05a/b/c | Z80N-SEL-01/02/03 | `nextreg_integration_test` |
| MMU defaults + port path | NR-MMU-01 | NREG-RST-05 | `nextreg_integration_test` |
| | NR-MMU-03 | P7F-01..P7F-08 | `mmu_test` |
| Port enables (G154 closure) | PE-06, PE-07, PE-08, PE-09 | PE-INT-82, PE-INT-86/87, PE-INT-89, PE-INT-80-88 | `nextreg_integration_test` |
| Copper register mask | COP-04 | ARB-04 (+ MOV-02/07) | `copper_test` |
| Write-only reads (G149 closure) | WO-01..WO-04 | WO-INT-04/29/35/60 (+ 2A/2B/63/FF) | `nextreg_integration_test` |
| +3 floppy I/O traps | FT-D8-01/02, FT-D9-01, FT-DA-01/02 | FT-INT-D8-01/02, FT-INT-D9-01a/b, FT-INT-DA-01a/b/c, FT-INT-DA-02 | `nextreg_integration_test` |

Two are WONT rather than covered — **COP-02** and **COP-03**, the cycle-accurate
CPU-vs-Copper `nr_wr_*` arbitration. jnext serialises both write paths at the
C++ call level, so the contested cycle the rows describe is a state the emulator
cannot enter; a row would have to order the stimulus by hand and would then be
asserting "the later write won", which is not what `zxnext.vhd:4769,4775-4777`
says. The decision is from 2026-04-21 and was re-confirmed by Task 65
(2026-07-17) in `doc/design/EMULATOR-DESIGN-PLAN.md` as option (a): documented
modelling limitation, not backlog.

Two became real `check()` rows:

- **PE-04** (`nextreg_test`, bare tier) — the `nr_85_internal_port_reset_type`
  gate at `zxnext.vhd:5052-5058`, asserted on both axes. The positive axis alone
  was already implied by `NREG-RST-08`, but not *discriminatively*: that row
  reads the group after a power-on reset, where reset_type is already '1' from
  its `:1230` initialiser, so an emulator that reloaded unconditionally would
  still pass it. The reset_type='0' axis is what pins the gate.
- **NR-MMU-04** (`nextreg_integration_test`) — last-writer-wins between the
  port-0x7FFD rebuild arm and the `nr_mmu_we` arm of the single MMU register
  process. `N8E-RAM-REBUILD-1` covered the rebuild-clobbers-NR direction; this
  row adds NR-clobbers-port and a closing second port write.

One stale claim was corrected rather than carried forward: **PE-06**'s stated
oracle ("internal-port-enable bits AND with reset_type-derived defaults; NOT
raw shadow") disagrees with the VHDL — `:5499` stores all eight written bits
and `:6128-6129` returns them verbatim, so the read on NR 0x82 *is* the raw
shadow. The live `PE-INT-82` already carries the corrected oracle.
