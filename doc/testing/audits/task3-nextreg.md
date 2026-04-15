# Subsystem NextREG row-traceability audit

**Rows audited**: 64 total (17 pass + 0 fail + 47 skip)

## Summary counts

| Status | GOOD | FALSE / LAZY | UNCLEAR |
|--------|------|--------------|---------|
| pass   | 17   | 0 | 0 |
| fail   | 0    | 0 | 0 |
| skip   | 47   | 0 | 0 |

**PLAN-DRIFT findings** (independent of status): 1

## FALSE-PASS
None identified.

## FALSE-FAIL
None identified.

## LAZY-SKIP
None identified.

## TAUTOLOGY
None identified.

## PLAN-DRIFT

### SEL-02 comment mismatch
- **Plan row text**: "Reset, read 0x243B — Returns 0x24 (protection default)"
- **Test file location**: `test/nextreg/nextreg_test.cpp:141-143`
- **Test comment text**:
  ```
  // VHDL: selector after reset is 0x24, so read_selected() == 0xA5.
  // Bare impl resets selected_ to 0, so read_selected() == 0x55.
  // Leave failing — tracked as emulator bug in Task 3 backlog.
  ```
- **VHDL reality**: `zxnext.vhd:4594-4596` specifies `nr_register <= X"24"` on reset. C++ `nextreg.cpp:15` implements `selected_ = 0x24;` — no bug.
- **C++ behaviour**: `selected_` is correctly reset to 0x24. Test passes correctly.
- **Why it's plan-drift**: The test comment describes an outdated failure scenario where the C++ allegedly reset to 0 instead of 0x24. The comment references a "Task 3 backlog" bug that does not exist — this is either (a) stale documentation from an earlier version, or (b) a copy-paste error from Task 2 item about SEL-02 divmmc false-pass. The test itself **passes correctly** and verifies the VHDL fact.
- **Suggested remediation**: Remove or rewrite the misleading comment. It should say:
  ```
  // VHDL: selector after reset is 0x24 (zxnext.vhd:4594-4596).
  // C++ resets selected_ to 0x24 in reset() (nextreg.cpp:15).
  // Test verifies selected_ points to NR 0x24 post-reset.
  ```
- **Does test assertion match VHDL regardless?** yes

## UNCLEAR
None identified.

## GOOD (summary only)

- **pass rows cleared**: 17
  - SEL-01: zxnext.vhd:4597-4599 port selection/read dispatch
  - SEL-02: zxnext.vhd:4594-4596 reset default selected_=0x24 (comment is misleading but test correct)
  - SEL-04: zxnext.vhd read dispatch NR 0x7F user scratch
  - RW-03: zxnext.vhd:5220 NR 0x12 L2 active bank plain round-trip
  - RW-04: zxnext.vhd:5200 NR 0x14 global transparent plain round-trip
  - RW-05: zxnext.vhd:5210 NR 0x15 sprite/lores control plain round-trip
  - RW-06: zxnext.vhd:5220 NR 0x16 L2 scroll X plain round-trip
  - RW-07: zxnext.vhd:5470 NR 0x42 ULANext format plain round-trip
  - RW-08: zxnext.vhd:5480 NR 0x43 palette control storage (sub_idx/auto-inc in integration tier)
  - RW-09: zxnext.vhd:5520 NR 0x4A fallback RGB plain round-trip
  - RW-10: zxnext.vhd:4607-4700 NR 0x50-0x57 MMU pages plain round-trip
  - RW-11: zxnext.vhd read dispatch NR 0x7F user scratch
  - RW-12: zxnext.vhd:5630 NR 0x6B tilemap control plain round-trip
  - MMU-02: zxnext.vhd:4613 NR 0x52 MMU2 page plain round-trip
  - PE-01: zxnext.vhd:2392-2442, 5052-5068 NR 0x82 port-enable plain round-trip (write=0x00)
  - PE-02: zxnext.vhd:2392-2442, 5052-5068 NR 0x82 port-enable plain round-trip (write=0xA5)
  - COP-01: zxnext.vhd:4706-4777 NR 0x15 CPU-path write (copper arbitration at integration tier)

- **skip rows cleared (GOOD-SKIP)**: 47
  - SEL-03: NR 0x00 read-only enforcement via integration-tier read_handler [zxnext.vhd read dispatch]
  - SEL-05: Z80N NEXTREG ED 91 opcode path at Z80 decoder tier, not NextReg public API
  - RO-01 through RO-06 (6 rows): All read-only registers (0x00 machine ID, 0x01 version, 0x0E sub-version, 0x0F board issue, 0x1E/0x1F video line) backed by FPGA generics or live counters, installed as read_handlers at integration tier
  - RST-01 through RST-09 (9 rows): Reset defaults owned by subsystems that wire write_handlers (Compositor, Layer2, SpriteEngine, Mmu, Input, TilemapEngine) not bare NextReg class
  - RW-01: NR 0x07 CPU speed read-side format transform (actual+requested packed) owned by speed FSM
  - RW-02: NR 0x08 bit7 read=NOT port_7ffd_lock asymmetry owned by Mmu
  - CLIP-01 through CLIP-08 (8 rows): Clip window 2-bit cycling indices live in Layer2/SpriteEngine/ULA/TilemapEngine write_handlers, not bare NextReg
  - MMU-01: MMU reset defaults owned by Mmu subsystem
  - MMU-03, MMU-04: Port 0x7FFD -> NR 0x56/0x57 coupling and last-writer-wins arbitration in Mmu subsystem
  - CFG-01 through CFG-05 (5 rows): NR 0x03 state machine (config mode FSM, XOR dt_lock, machine-type gating) owned by machine-type-manager write_handler
  - PAL-01 through PAL-06 (6 rows): Palette-write pipeline (sub_idx latch, palette RAM, priority bits, auto-increment) in palette subsystem (Layer2/Compositor), not bare NextReg
  - PE-03: NR 0x82 bit6 joystick-port decode gating in PortDispatch, not NextReg
  - PE-04, PE-05: Internal/bus port-enable reset defaults (0xFF) wired at integration tier
  - COP-02, COP-03, COP-04: Copper arbitration (simultaneous write, CPU-wait, 7-bit register mask) cycle-accurate state not exposed by bare NextReg API

## Audit methodology notes

**Bare-vs-integration tier boundary correctly respected**: Every skip row cites a subsystem (MMU, Layer2, SpriteEngine, Compositor, etc.) that owns the register's behaviour. The test file preamble (lines 16-39) correctly articulates the decision: these rows are not tautological (testing bare NextReg's stub); they must be deferred to the full-machine integration tier where the subsystems are wired via handlers. This discipline is sound.

**VHDL citations accurate**: All pass-row citations to zxnext.vhd line ranges are pinpoint-correct. Line ranges like 4594-4596 (reset default), 4597-4599 (port load), 5200 (register storage) all verify against the actual VHDL.

**C++ implementation matches VHDL for bare scope**: The `NextReg` class (nextreg.h/cpp) correctly implements the bare-API-tier contract:
- `selected_` resets to 0x24 [VHDL 4594-4596] ✓
- `read(r)` returns cached `regs_[r]` if no read_handler, else calls handler [per design] ✓
- `write(r, v)` stores then calls write_handler [per design] ✓
- No read-only enforcement, clip cycling, palette state, or copper arbitration (integration-tier facilities) ✓

**One stale comment needs cleanup** (SEL-02): The inline comment describing a reset-to-0 bug that does not exist should be corrected to reflect the current (correct) implementation. This is minor documentation debt, not a test defect.

**No FALSE-PASS, FALSE-FAIL, LAZY-SKIP, or TAUTOLOGY identified**. All 17 passing rows are legitimate round-trip assertions on plain storage registers with correct VHDL citations. All 47 skipped rows are properly justified integration-tier deferrals with VHDL line citations in the skip reason text.

**Audit clearance**: NextREG subsystem is traceability-clean per VHDL-oracle discipline.
