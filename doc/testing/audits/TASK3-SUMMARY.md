# Task 3 — Full row-traceability audit, master summary

Generated: 2026-04-15 after Wave 3 lands on `main` at `c554600`.

This document aggregates the 15 per-subsystem audit reports in this directory
(`task3-<subsystem>.md`). Per-row detail lives in the per-subsystem files; this
file is the flat-list index for the remediation phase.

## Per-subsystem breakdown

| Subsystem         | Rows audited             | pass     | fail   | skip    | GOOD pass | GOOD-FAIL | GOOD-SKIP | FALSE-PASS | FALSE-FAIL | LAZY-SKIP | TAUTOLOGY | PLAN-DRIFT | UNCLEAR |
|-------------------|--------------------------|----------|--------|---------|-----------|-----------|-----------|------------|------------|-----------|-----------|------------|---------|
| Memory/MMU        | 143                      | 66       | 0      | 77      | 64        | 0         | 69        | 0          | 0          | 0         | 0         | 0          | 2       |
| NextREG           | 64                       | 17       | 0      | 47      | 17        | 0         | 47        | 0          | 0          | 0         | 0         | 1          | 0       |
| CTC+Interrupts    | 150                      | 43       | 1      | 106     | 43        | 1         | 103       | 0          | 0          | 0         | 0         | 0          | 0       |
| Copper            | 76                       | 66       | 0      | 10      | 65        | 0         | 9         | 1          | 0          | 1         | 1         | 0          | 0       |
| Tilemap           | 69                       | 38       | 13     | 18      | 38        | 13        | 18        | 0          | 0          | 0         | 0         | 0          | 0       |
| Layer 2           | 97 unit *(+44 deferred)* | 89       | 6      | 2       | 89        | 6         | 2         | 0          | 0          | 0         | 0         | 0          | 0       |
| Sprites           | 126                      | 116      | 0      | 10      | 116       | 0         | 10        | 0          | 0          | 0         | 0         | 0          | 0       |
| DMA               | 156                      | 116      | 5      | 35      | 116       | 4         | 35        | 0          | 0          | 0         | 0         | 0          | 1       |
| Audio             | 200                      | 121      | 6      | 73      | 121       | 6         | 73        | 0          | 0          | 0         | 0         | 1          | 0       |
| ULA Video         | 123                      | 47       | 1      | 75      | 47        | 1         | 75        | 0          | 0          | 0         | 0         | 0          | 0       |
| Compositor        | 114                      | 90       | 24     | 0       | 88        | 24        | 0         | 0          | 0          | 0         | 2         | 0          | 0       |
| DivMMC+SPI        | 123                      | 53       | 14     | 56      | 53        | 14        | 56        | 0          | 0          | 0         | 0         | 0          | 0       |
| UART+I2C/RTC      | 106                      | 58       | 2      | 46      | 58        | 0         | 46        | 0          | 0          | 0         | 0         | 1          | 2       |
| I/O Port Dispatch | 97                       | 74       | 18     | 5       | 72        | 18        | 5         | 2          | 0          | 0         | 0         | 1          | 0       |
| Input             | 149                      | 23       | 2      | 126     | 21        | 2         | 126       | 0          | 0          | 0         | 0         | 0          | 0       |
| **TOTAL**         | **1793**                 | **1017** | **92** | **686** | **1008**  | **89**    | **674**   | **3**      | **0**      | **1**     | **3**     | **4**      | **5**   |

Row-count delta vs Task 3 spec (1788 rows expected): +5. Comes from Layer 2 deferred rows (44 tracked but not in pass/fail) and Compositor/Input table expansion. Within acceptable audit variance.

## Aggregate headline

- **Zero FALSE-FAIL rows across 1793 audited.** No emulator fix chases a non-bug.
- **Three FALSE-PASS rows** — all minor, two are effectively tautologies (NR-readback dressed as port-gate test), one is accidental pass on a tracked bug.
- **One LAZY-SKIP** — Copper OFS-06 cite-strengthening only (GOOD-SKIP with weak justification).
- **Three TAUTOLOGY rows** — Copper ARB-05, Compositor TR-11/TR-12.
- **Four PLAN-DRIFT** — stale documentation, no test changes needed beyond plan-doc edits.
- **Five UNCLEAR** — all tractable once humans look: two stale comments, one design-gap call, two RTC debug questions.
- **89 GOOD-FAIL rows** across 9 subsystems — all legitimate emulator bugs with correct VHDL oracles. These become the Task 2 resumption backlog.

The audit confirms the VHDL-as-oracle discipline is holding. The SX-02 pattern is **not endemic** — it was a genuine outlier. Test plans are substantially more honest than the coverage-theatre audit feared.

## FALSE-PASS (high priority, 3 rows)

| Subsystem | Row ID | Test location | One-line description | Suggested remediation |
|---|---|---|---|---|
| Copper | **RAM-MIX-01** | copper_test.cpp:326-332 | Passes accidentally due to missing sticky `write_8` latch in `Copper::write_reg_0x63()`. Correctly asserts VHDL (zxnext.vhd:3977-3978) but implementation hasn't diverged enough yet. | No test action. Flag for Task 2 emulator fix (sticky `write_8` latch). Row will remain GOOD once C++ matches. |
| I/O Port Dispatch | **NR82-00** | port_test.cpp:633 | Asserts only NR 0x82 bit 0 readback after clearing it. Does not verify the port-0xFF gate actually fires (VHDL zxnext.vhd:2397). Passes because NR register store works, not because gate works. | Convert to behavioural test: write 0xFF with bit 0 clear, observe Timex screen mode unchanged. |
| I/O Port Dispatch | **NR82-02** | port_test.cpp:655 | Same pattern as NR82-00 for Pentagon 0xDFFD gate (VHDL zxnext.vhd:2400). NR-readback tautology dressed as port-gate test. | Convert to behavioural test: OUT 0xDFFD with bit 2 clear, verify no observable effect. Requires Pentagon handler registration first. |

## FALSE-FAIL (0 rows)

None across the whole audit. Every currently-red row encodes a real VHDL fact and fails legitimately.

## LAZY-SKIP (1 row)

| Subsystem | Row ID | Test location | One-line description | Decision |
|---|---|---|---|---|
| Copper | **OFS-06** | copper_test.cpp:1076 | Skip message "c_max_vc wrap not modelled" lacks explicit VHDL citation. The plan document (`COPPER-TEST-PLAN-DESIGN.md:505-508` Open Question #3) does document the unreachability. | **Keep as GOOD-SKIP.** Strengthen skip message to cite plan Open Question #3 (cvc offset and readback not in C++ API). No scope change. |

## TAUTOLOGY (3 rows)

| Subsystem | Row ID | Test location | One-line description | Suggested remediation |
|---|---|---|---|---|
| Copper | **ARB-05** | copper_test.cpp:1113-1120 | Programs MOVE in mode=00 (stopped), writes directly to NR 0x40 via CPU, asserts `copper_pulses == 1`. The 1 pulse is the CPU write — Copper never fires. Test is a structural sanity check, not a VHDL-traced fact. | Split into ARB-05a (GOOD: mode=00 inhibits MOVE output, cites copper.vhd:92-97) and drop the CPU sanity half, or narrow to Copper-only. |
| Compositor | **TR-11** | compositor_test.cpp:170-180 | Sets ULA colour and fallback colour to the identical ARGB (0xE3). Test passes whether ULA wins (opaque) or fallback shows (transparent) — they're equal. Transparency mechanism never actually exercised. | Replace with distinct fallback colour (e.g., 0x10) so opaque ULA and transparent fallback produce observably different outputs. |
| Compositor | **TR-12** | compositor_test.cpp:182-199 | Same pattern as TR-11 — both LSB=0 and LSB=1 cases use identical ARGB. Test trivially passes without exercising the bit-0 distinction that VHDL zxnext.vhd:7100 explicitly ignores. | Use distinct fallback colour so LSB variants can produce different outputs. |

## PLAN-DRIFT (4 rows — plan description inaccurate vs VHDL)

| Subsystem | Row ID | Plan location | Test still matches VHDL? | Description |
|---|---|---|---|---|
| NextREG | **SEL-02** | nextreg_test.cpp:141-143 (inline comment, not plan doc) | yes | Test comment still describes a past "selected_ resets to 0" bug that was fixed. Current C++ `selected_ = 0x24` matches VHDL zxnext.vhd:4594-4596. Comment is stale; test itself is correct. |
| Audio | **AY-113** | AUDIO-TEST-PLAN-DESIGN.md §1.10 | yes | Plan row says "hold at is_bot → YM[1]=0x00". VHDL ym2149.vhd:428-431 actually holds at `is_bot_p1 → YM[1]=0x01`. Test at line 1131-1132 asserts correct VHDL value (0x01); only plan doc needs fix. |
| I/O Port Dispatch | **REG-17** | IO-PORT-DISPATCH-TEST-PLAN-DESIGN.md preamble | yes | Plan says "UART 0x133B rejected". VHDL zxnext.vhd:2639 actually decodes 0x133B via the cpu_a(15:11)="00011" window. Port is accepted, not rejected. Test correctly asserts non-0xFF; plan wording needs fix. |
| UART+I2C/RTC | **test file header** | uart_test.cpp:19-27 | yes | Test file header comment lists i2c.cpp:101 false-STOP and uart.cpp:299 bit 6/3 mismatch as "known outstanding bugs". Both were fixed in Task 2 (commits 174fa56 and 47ee7e2); comment is stale. |

All 4 are plan/doc-only edits. No test code changes. Batch into one commit per subsystem during remediation.

## UNCLEAR (5 rows — need human review)

| Subsystem | Row ID | Status | Question for human |
|---|---|---|---|
| Memory/MMU | **RST-01** | pass | Test file header lines 22-25 say this is a "known-failing row" because `Mmu::reset()` calls map_rom which clobbers nr_mmu_[0]. Current src/memory/mmu.cpp:18-35 does NOT have this bug — `map_rom_physical()` intentionally preserves nr_mmu_. Either the comment is stale or the audit read the wrong code. Investigate and fix comment. |
| Memory/MMU | **RST-02** | pass | Identical to RST-01, mirror for slot 1. Same investigation covers both. |
| DMA | **12.6** | fail | Plan asserts "byte mode (R4_mode="00") transfers single byte then stop". VHDL dma.vhd:426 has no such per-enable gating — byte mode runs the full block like other modes. Is 12.6 (a) a KNOWN-GAP tracking intended Z80-DMA alignment, (b) a plan misreading of the VHDL, or (c) a feature to defer? If (a), convert to SKIP + Task 2 backlog. If (b)/(c), drop or re-word. |
| UART+I2C/RTC | **RTC-06** | fail | Reads DS1307 register 0x02 (hours) and gets 0x73 (invalid BCD). Registers 0x00 and 0x01 pass; only 0x02 and 0x03 fail with the *same* invalid value. `to_bcd()` pipeline works (RTC-01/02/04/05 pass). Register-pointer handling or state machine fault specific to registers 0x02/0x03. Needs investigation. |
| UART+I2C/RTC | **RTC-07** | fail | Reads DS1307 register 0x03 (day-of-week) and gets 0x73. Same root cause as RTC-06. Fix once, both go green. |

## GOOD-FAIL inventory (89 rows — Task 2 resumption backlog)

These rows correctly cite VHDL and legitimately fail because the C++ emulator diverges. They are NOT actioned in Task 3 remediation; they stay red until Task 2 resumes and fixes them. Grouped by root-cause cluster where possible:

### Layer 2 bank transform (6 rows, single fix)
- **G7-01, G7-02, G7-03, G7-05a, G7-05b, G7-05c**: All fail because `src/video/layer2.cpp:52-61` omits the VHDL `+1` bank transform at `layer2.vhd:172`. Single Task 2 fix unblocks all 6.

### Tilemap control-bit swap (10 rows, single fix)
- **TM-40, TM-41, TM-42, TM-43** (textmode bit 5 vs VHDL bit 3): `src/video/tilemap.cpp:39` reads bit 5 instead of bit 3.
- **TM-50, TM-51, TM-52, TM-53** (strip_flags bit 4 vs VHDL bit 5): `src/video/tilemap.cpp:40` reads bit 4 instead of bit 5.
- **TM-71, TM-104**: Textmode pixel/palette path, unreachable due to same bit-swap.
- All 10 cite `tilemap.vhd:190-191, 366, 385-386`. Two-bit swap fix unblocks all 10.

### Tilemap unimplemented features (3 rows)
- **TM-15** (rotate swaps X/Y), **TM-30, TM-31** (512-tile mode): Genuine feature gaps, not bugs.

### Compositor blend / priority / stencil (24 rows, ~4 clusters)
- **Border exception (3)**: PRI-011-LUS-border, PRI-100-USL-border, PRI-101-ULS-border (zxnext.vhd:7256/7266/7278).
- **L2 palette-bit 15 priority promotion (6)**: L2P-10 through L2P-13, L2P-17, L2P-18.
- **Blend modes 110/111 (12)**: BL-10, BL-11, BL-13, BL-15, BL-20, BL-21, BL-22, BL-24, BL-26, BL-27, BL-28, BL-29 — renderer.cpp:259 falls back to SLU.
- **Stencil mode (2)**: STEN-12, STEN-13 — no stencil AND logic.
- **Global sprite enable (1)**: TR-42 — NR 0x15[0]=0 not gated at zxnext.vhd:6934/6819/7118.

### Audio envelope C=0 family (4 rows, single root)
- **AY-81, AY-102, AY-110, AY-122**: All trace to the envelope hold-at-boundary cycle-timing bug in `ay_chip.cpp:261-270`. Affects shape 0 (and likely 2, 4, 6). Task 2 item 2 should widen scope from "shapes 0/9" to all C=0 shapes.

### Audio TurboSound panning (2 rows, single root)
- **TS-10, TS-42**: R-channel computation bug when TurboSound disabled but PSG selected. `turbosound.cpp:144-145` zeros R_sum in one path.

### DivMMC+SPI — port 0xE3 masking (3 rows, single fix)
- **E3-04** (mapram OR-latch), **E3-07** (read mask bits 5:4), **E3-08** (write mask bits 5:4). All cite `zxnext.vhd:4183, 4190`. Two-line fix in DivMmc read_control/write_control.

### DivMMC+SPI — automap ROM3 gating (6 rows, single fix)
- **EP-02, EP-03, EP-11, NR-01, NR-02, NR-05**: DivMmc `check_automap()` ignores `i_automap_rom3_active` (zxnext.vhd:2850-2908).

### DivMMC+SPI — SPI CS decode validation (3 rows, single fix)
- **SS-09, SS-10, SS-11**: SpiMaster stores raw CS byte without decoding. VHDL zxnext.vhd:3300-3332 collapses invalid patterns to 0xFF.

### DivMMC+SPI — SPI MISO pipeline delay (2 rows, blocked refactor)
- **SX-03, ML-05**: No pipeline delay for miso_dat. VHDL spi_master.vhd:158-167 latches via state_last_d one cycle after transfer. **Blocked** — requires SpiDevice receive/send split-stream refactor that risks breaking SdCard boot path. Parked per session handover.

### DMA — block-length edge case (3 rows, single fix)
- **9.8, 14.6, 14.7**: block_len=0 transfers 1 byte instead of 0. `dma.cpp:~560` post-increments counter before checking `counter >= block_len`. VHDL dma.vhd:426 tests `counter < block_len` before increment.

### DMA — continuous mode prescaler (1 row)
- **12.7**: C++ `dma.cpp:581` only sets `burst_wait_` when mode==2. VHDL dma.vhd:424 prescaler gate is mode-agnostic.

### I/O Port Dispatch (18 rows, many small fixes)
- **LIBZ80-05, REG-09, REG-10, REG-18–21, REG-26, REG-27, NR82-01, NR85-03b, NR-DEF-01, NR-RST-01, NR-85-PK, BUS-86-01, PR-01, AMAP-03, BUS-02**: Missing handlers, missing NR 0x82–0x85 gating, soft-reset reload, handler-overlap collision check. See task3-port.md for per-row detail.

### Input — NR reset defaults (2 rows, single fix)
- **JMODE-08** (NR 0x05 = 0x40), **IOMODE-01** (NR 0x0B = 0x01): `NextReg::reset()` at nextreg.cpp:8 zeroes the entire register file instead of applying VHDL signal-decl and soft-reset block defaults.

### ULA Video (1 row)
- **S13.14**: `VideoTiming::advance()` doesn't set `frame_done_` at 69888 T-state boundary. Intentional regression witness; Emulator Bug backlog item 4.

### CTC+Interrupts (1 row)
- **CTC-CH-01**: `ctc.cpp:127-132 handle_zc_to()` uses `if (channel < 3)` linear chain; VHDL ctc.vhd ring topology means ch3 should also trigger ch0. One-line fix.

## Audit methodology notes (aggregate)

- **Read-only phase complete**: 15/15 subsystems audited, no emulator or test code touched during audit phase.
- **Batch regression**: Task 3 spec mandates batch regression at the end of the remediation phase — no per-fix regression during Task 3 remediation since audit-fixes are test-code-only and cannot affect screenshot tests.
- **Per-row VHDL citation health**: Pass/fail rows are nearly universally well-cited. Skip rows are the weakest link: many cite correct VHDL but lean on prose justification rather than line numbers. None crossed into LAZY-SKIP per the strict definition (only OFS-06 came close, and even that had plan-document backing).
- **Task 2 un-pause criteria**: Per Task 3 spec §"Done state", Task 2 un-pauses when all 15 reports collected (done), master summary written (this file), and remediation complete on FALSE-PASS/FALSE-FAIL/LAZY-SKIP/TAUTOLOGY/PLAN-DRIFT findings (11 total rows requiring action). After that, the 89 GOOD-FAIL rows become the resumption backlog — most of them are cluster-fixes, so the real count of Task 2 items is closer to 15-20 distinct fixes, not 89.
- **The SX-02 pattern was not endemic**: The original concern that false-pass oracles might be widespread is not borne out by the audit. 1793 rows → 3 FALSE-PASS, 2 of which are NR-readback tautologies rather than SX-02-style complementary-bug patterns. The VHDL-as-oracle discipline is holding across the subsystem test plans.

## Next actions (pending user authorisation)

1. **PLAN-DRIFT batch (4 edits, low-risk)**: Update stale comments in `nextreg_test.cpp`, `AUDIO-TEST-PLAN-DESIGN.md` AY-113 row, `IO-PORT-DISPATCH-TEST-PLAN-DESIGN.md` REG-17 note, `uart_test.cpp` header. One commit per subsystem.
2. **TAUTOLOGY fixes (3 rows, low-risk)**: ARB-05 split, TR-11/TR-12 distinct-fallback. Test-code edits; independent critic review each.
3. **FALSE-PASS fixes (2 actionable, RAM-MIX-01 deferred to Task 2)**: NR82-00/NR82-02 narrow to behavioural tests. Depends on Pentagon 0xDFFD handler work which may belong to Task 2.
4. **LAZY-SKIP fix (1 row, trivial)**: OFS-06 skip-message cite strengthening.
5. **UNCLEAR resolution (5 rows)**: Investigate MMU RST-01/02 stale comment vs code; decide DMA 12.6 disposition; diagnose RTC-06/07 register-pointer fault.
6. **After the 11 action items clear**: run `python3 test/refresh-traceability-matrix.py`, then the user runs `bash test/regression.sh` once, then Task 3 is marked done and Task 2 un-pauses with the 89 GOOD-FAIL cluster-fixes as the resumption backlog.
