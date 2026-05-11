# NextZXOS Boot Critical Subsystem Analysis — Aggregated Report

**Branch:** `nextzxos-boot-subsystem-analysis` (off `main`)
**Date:** 2026-05-09
**Audit scope:** memory, divmmc + sd_card + spi, nmi + multiface + port + nextreg, cpu (Z80 + Z80N + IM2)
**Methodology:** parallel analysis agents (one per subsystem, ultrathink mode, VHDL-as-oracle). The audit is being repeated **iteratively** with a fresh set of four blind agents each pass — each pass blind to all prior reports — until reviews honestly converge to zero class-(a) bugs across all four subsystems. The user's standing instruction: "correctness is the priority, not premature convergence."

## Executive summary

| Pass | Method | Class-(a) bugs found | Notes |
|---|---|---|---|
| 1 | 4 analysis + 4 reviewers | 11 + 1 follow-up | All reviewers APPROVE-WITH-NITS |
| 2 | 4 fresh blind | 6 | Two high-leverage G46(b) candidates |
| 3 | 4 fresh blind | 11 | Refinements + class-(b) promotions |
| 4 | 4 fresh blind — sharper methodology | 17 | NMI 80% sweep + CPU duals |
| 5 | 4 fresh blind — final-convergence angles | 11 | NR $09/$15 cross-fix; LDPIRX confirmed; Z80N M1 contention |
| 6 | 4 fresh blind — cycle-precise + final-NR-coverage + operand contention | 4 | One per subsystem |
| 7 | 4 fresh blind — convergence-test pass | 7 | NMI same-shape pattern systemic |
| 8 | 4 fresh blind — stricter (resolve class-b) | 14 + 11 class-b resolved | All 4 subsystems resolved class-b backlog |
| 9 | 4 fresh blind — strictest (resolve class-c) | 10 + 11 class-c resolved | All 4 subsystems at zero class-a/b/c. 4 class-(d) architectural items escalated |
| 10 | 4 fresh blind — convergence test | **5 + 1 class-c fixed; 5 class-c catalogued** | NOT converged; 1 more class-(d) (CPU IM2 controller bridge) |
| 11 | 4 fresh blind — combined fix+test+commit mandate; reviewer + fix-of-reviewer + fix-reviewer | **0 class-a, 2 class-b, 6 class-c (incl. NIT)** | All 8 findings fixed with discriminative regression tests; reviewer found 1 missed NIT (NR $06 cache leak) which was fixed and re-verified; sibling audit confirmed cache-leak family closed. No class-(d) escalations introduced. NOT converged honestly (8 new bugs → must continue). Plus cross-cutting Release-build CMake fix |
| 12 | 4 fresh blind; pipelined audit→reviewer→fix-of-reviewer→fix-reviewer | **1 class-a, 4 class-b, 7 class-c (incl. NITs)** | Memory 3b+2 NITs; DivMMC 1b+1c audit + 3 reviewer-promoted class-c + 1 NIT (8 fixes total) + 3 class-d confirmed (architectural); NMI-MF-Port 1a + 1 NIT; CPU 0 audit findings (defensible-zero) + 2 reviewer NITs. ALL 12 fixes have discriminative regression tests + independent fix-reviewer APPROVE. NOT converged (12 new bugs). Subsystem-skip rule introduced 2026-05-10: subsystems with audit=0 + reviewer APPROVE-no-missed are skipped in subsequent passes — none qualified yet (CPU was closest: audit=0 but reviewer found NITs) |
| 13 | 4 fresh blind; pipelined; comment-only-skip rule introduced | **2 class-a, 1 class-b, 1 class-c + 1 class-d listed + 3 comment NITs** | Memory 1a (V13-MEM-01 NR $69 bit 7 cross-subsystem mirror gap) APPROVE; DivMMC 1c (V13-DIVMMC-01 CMD24 past-EOF early R1) APPROVE; NMI-MF-Port 1b (V13-NMP-01 NR $05 Pentagon cache-leak — same family as V11-NMP-02/03) APPROVE-WITH-NITS (3 comment NITs fixed without fix-reviewer per new skip rule); CPU 1a (V13-CPU-01 DJNZ IncDecZ polarity inverted; rewrote pass9_ldws test that enshrined the bug) + 1 class-d (V13-CPU-D1 IM2 controller bridge — confirmed architectural) APPROVE. NOT converged. Trend: Pass-11 = 8, Pass-12 = 17 effective, Pass-13 = 7 effective — descending |
| 14 | 4 fresh blind; pipelined | **4 class-a, 0 class-b, 5 class-c (incl. NITs) + 1 comment NIT — Memory CONVERGED** | **Memory ZERO findings + reviewer APPROVE-no-missed → CONVERGED** (skipped from Pass-15 onward); DivMMC 2c (V14-DIVMMC-01 CMD18 mid-stream past-EOF, V14-DIVMMC-02 CMD8 R7 R1-prefix) APPROVE-WITH-NITS + 1 comment NIT; NMI-MF-Port 3a (V14-NMP-01 MF+3 FDC-gated readback rewriting MF-MUX-01 enshrined-bug test, V14-NMP-02 NR $28 nr_stored_palette_value, V14-NMP-03 NR $2B WO returns 0) + reviewer-promoted V14-NMP-04 (NR $2A WO returns 0) APPROVE; CPU 1a (V14-CPU-01 INC/DEC BC IncDecZ update missing) + reviewer NIT V14-CPU-NIT-01 (DD/FD-prefix walk — closes V13-CPU-01 DJNZ-prefix sibling as side effect) APPROVE-WITH-NITS → fix-reviewer APPROVE. IncDecZ shadow family closed; past-EOF SD token family closed; multi-writer fan-out family closed. Trend: Pass-14 = 9 effective findings — comparable to Pass-13 |
| 15 | 3 fresh blind (memory skipped); pipelined | **2 class-a, 0 class-b, 2 class-c + reviewer-promoted NIT-3 = 5 effective findings + 2 class-d architectural** | DivMMC 1c (V15-DIVMMC-01 SD CMD24 silent write-success on RO/failbit; closes write-success false-positive family) APPROVE; NMI-MF-Port 2a (V15-NMP-01 NR $63, V15-NMP-02 NR $FF write-only readback cache leaks) APPROVE — closes WO-NR set per VHDL :6286-6287 fall-through; CPU ZERO audit findings (defensible-zero across 16 angles) + reviewer APPROVE-WITH-NITS — NIT-3 (ULA+ port contention not propagated CPU-side) **rejected as deflection, fixed inline by reviewer with 5 disc tests on contention path**, NIT-1+2 properly re-classified class-(d) ARCHITECTURAL (DD-ED-Z80N: Z80N uses Alternate not XY_State; M1-strobe per-byte: FUSE boundary). **CPU NOT yet converged per strict rule** (reviewer found missed finding even though it fixed it; Pass-16 must re-test) |
| 16 | 3 fresh blind (memory skipped); pipelined; 1 audit-was-doc-only requiring fix-of-audit | **3 class-a, 2 class-b, 2 class-c = 7 effective findings** | DivMMC 2c (V16-DIVMMC-01 port $E7 write-only nullptr read handler per VHDL :2803-2806; V16-DIVMMC-02 FAT32 directory cycle DOS hardening) APPROVE; CPU 1a (V16-CPU-01 load_state shadow re-push of port_ulap_io_en_; same family as V12-MEM-02) APPROVE; **NMI-MF-Port 2b** (V16-NMP-01 NR $10 SPKEY_BUTTONS readback per VHDL :5924; V16-NMP-02 expbus AND-mask NR $86-$89 with NR $82-$85 per VHDL :2392-2393) — **audit was doc-only**, fix-of-audit applied (33 port-decode sites routed through new `effective_internal_port_enable` helper + 13 disc tests) + fix-reviewer APPROVE. Merge conflict in emulator.cpp resolved cleanly (V16-DIVMMC + V16-CPU + V16-NMP all touch same file). NOT converged — all 3 active subsystems still find class-(a/b/c) bugs |
| 17 | 3 fresh blind (memory skipped); pipelined; user added "find as many bugs as possible per pass" mandate | **0 class-a, 4 class-b, 4 class-c + 1 class-d listed = 8 effective findings** | DivMMC 1c (V17-DIVMMC-01 ACMD41 HCS bit not reflected in CMD58 OCR CCS bit per `sdcard` VHDL — pre-fix CMD58 unconditionally reported CCS=1 even with HCS=0) APPROVE-no-missed + 1 class-d listed (V17-DIVMMC-02 cycle-accurate SPI master FSM, G137 scope); NMI-MF-Port 3b (V17-NMP-01 NR $B8-$BB readback returns 0x00 instead of VHDL reset defaults 0x83/0x01/0x00/0xCD per zxnext.vhd :5087-5090; V17-NMP-02 port_FE port-decode matches any even port per VHDL :2582; V17-NMP-03 port_FF port-decode matches any LSB==$FF port per VHDL :2540-2571,:2583) APPROVE-no-missed; CPU 1b + 2c (V17-CPU-01 IM2 `im2_int_req` latch held at 0 in pulse mode per im2_peripheral.vhd:170-171 — phantom IM2 interrupt assertion on pulse→IM2 mode transition; V17-Z80N-01a/b BSRF/BSLA strict-UB-free shifts per t80n.vhd:992,:1006-1014) APPROVE-WITH-NITS, reviewer added V17-CPU-NIT-04 (BSRA strict-UB-free shift, sibling of V17-Z80N-01a/b — same UB family on signed `int16_t >> n`), fix-reviewer APPROVE. ctc_test::IM2W-07 also updated (pre-fix relied on V17-CPU-01 bug) — reviewer verified update is VHDL-faithful (drop+re-raise across mode switch is the only S_REQ-entry path per VHDL). NOT converged — 3 active subsystems still find class-(b/c) bugs, but trend is descending (Pass-16 = 7, Pass-17 = 8 effective) and DivMMC dropped to 1 finding. Cross-cutting Pass-16 follow-up: regression.sh rewind-func regex made resilient to test-count growth |
| 18 | 3 fresh blind (memory skipped); pipelined; "find as many bugs as possible per pass" emphasis preserved | **0 class-a, 4 class-b, 5 class-c (incl. NIT cluster) + 1 NIT = 10 effective findings; 1 class-d listed re-confirmed** | DivMMC 0 audit findings (defensible-zero claim) + reviewer flagged 1 class-c missed: V18-DIVMMC-NIT-01 SdCardDevice full-duplex stream advance in default branch per `serial/spi_master.vhd:104-117,148-168` + `zxnext.vhd:3270-3298` (write-side path was returning bare $FF instead of clocking next response byte; fix delegates to `send()`); fix-reviewer APPROVE. NMI-MF-Port 4b audit (V18-NMP-01 Kempston mouse 0xFADF/FBDF/FFDF mask 0xFFFF→0x0FFF per zxnext.vhd :2668-2670; V18-NMP-02 Profi-Covox 0x003F/0x005F mask 0xFFFF→0x00FF per :2661,:2664; V18-NMP-03 SD2 DAC 0xF1/F3/F9/FB mask 0xFFFF→0x00FF per :2661-2664; V18-NMP-04 GS Covox 0x00B3 mask 0xFFFF→0x00FF per :2659) + reviewer-added V18-NMP-NIT-01 cluster (10 ports missing port_*_io_en gates per NR 0x82/0x83/0x85: sprite 0x303B/0x57/0x5B b6, layer2 0x123B b7, ULA+ 0xBF3B/0xFF3B b0, CTC 0x183B..1F3B b3, DMA 0x6B b5, DMA 0x0B b1) — fix applied 9 sites + 8 disc tests + 1 SKIP, fix-reviewer APPROVE. CPU 0 audit findings (defensible-zero claim) + reviewer **REJECTED** with 2 missed class-c: V18R-CPU-01 INT pulse-expired drop unconditional of IFF1 per VHDL zxnext.vhd:2017-2033; V18R-CPU-02 raise(Im2Level::DMA) DevIdx-collision pollutes CTC7 int_status per im2_peripheral.vhd:160 + zxnext.vhd:4092 (CTC4..CTC7 hardwired 0); + V18R-CPU-NIT-01 LDPIRX MEMPTR-lo strobe per t80n_mcode.vhd:1967 — 3 fixes + tests + golden update (edb7_basic/edb7_skip 0x0000→0x00B7), fix-reviewer APPROVE. **No subsystem converged this pass.** Trend: P11=8, P12=17, P13=7, P14=9, P15=5, P16=7, P17=8, P18=10 — slight uptick driven by reviewer-found missed findings. Lesson: defensive-zero audit claims still missed material bugs in 2/3 subsystems, suggesting auditors stopped scanning too early despite the thoroughness mandate. Pre-existing parallax-demo regression test failure verified to predate Pass-17 (also FAILs at Pass-16 head `f718876` with same 44636 pixel diff — Pass-16 handover memory's "regression 33/0/0" claim was incorrect, bug is older) |
| 19 | 3 fresh blind (memory skipped); **NEW: mandatory enumeration table at top of every audit report** per `feedback_task2_audit_enumeration_table.md`; pipelined | **0 class-a, 4 class-b, 4 class-c + 2 cosmetic doc NITs = 10 effective findings; V13-CPU-D1 architectural item ELEVATED to class-b and FIXED** | DivMMC 80-row table + 1c audit F19-DIVMMC-NIT-01 (port_e3_reg bits 5:4 stored unmasked per VHDL :4173-4190 invariant '00') + 1d listed F19-DIVMMC-D01 (cycle-accurate spi_wait_n; G137) + reviewer APPROVE-no-missed (10/10 spot-check passed). NMP ~270-row table + 0 audit findings (defensive-zero) + reviewer APPROVE-WITH-NITS (V19R-NMP-NIT-01+02 cosmetic doc-only table corrections; V19R-NMP-NIT-03 NR 0xF0 XADC composed-read stub returns 0x00 per VHDL :6273-6275 Issue2; V19R-NMP-NIT-04 NR 0xF8 bit-7 mask per VHDL :6277-6278) + fix-reviewer APPROVE. **CPU ~120-row table + 4 class-b MAJOR audit findings (all IM2 integration-wiring gaps)**: V19-IM2-01 NR 0x22 b1 → dev_[LINE].int_en per VHDL :5297,:5610; V19-IM2-02 port_ff_reg(6) → dev_[ULA].int_en per VHDL :3614-3622; V19-IM2-03 int_unq one-shot clear at end of tick per VHDL :1946-1947 nr_20_we; **V19-IM2-04 int_line_asserted polling drives CPU /INT per VHDL :1840 z80_int_n composition — resolves V13-CPU-D1 architectural item by elevation to class-b**. + reviewer APPROVE-WITH-NITS (V19R-CPU-01 missed: int_req 1-cycle pulse synthesis per VHDL im2_peripheral.vhd:90-101 — IM2 peripheral level→pulse antipattern; before fix, ULA/LINE/CTC INT fired once per emulator session in IM2 mode) + fix-reviewer APPROVE. **Pass-20 follow-up flagged**: V20-NMP-XADC (NR 0xF9/0xFA same family as NIT-03/04). **Trend**: P18=10, **P19=10**. **Key result**: enumeration table directly surfaced V19-IM2-01..04 — exactly the kind of structural integration-wiring gaps the table was designed to expose; quality of findings up sharply (V19-IM2-04 alone was a multi-pass architectural item resolved in single audit). Regression 33/0/0 (parallax-demo reference regenerated). |
| 20 | 3 fresh blind (memory skipped); enumeration-table mandate continued; pipelined | **0 class-a, 2 class-b, 5 class-c (3 audit + 2 reviewer-NIT) + 2 cosmetic doc NITs = 9 effective findings; 3 class-d listed re-confirmed** | DivMMC 75/98-row table + 1c V20-DIVMMC-01 (CMD55+ACMD41 R1 missing APP_CMD bit 5 per SD Phys Layer Spec § 7.3.2.1; SD-19/SD-20 existing-tests UPDATED to spec-correct values + SD-32 new disc test) + 3d listed (V20-DIVMMC-D01..D03 pre-existing architectural) + reviewer APPROVE-no-missed (10/10 spot-check, SD-19/20 update verified legitimate bug-correction). NMP ~270/294-row table + 2c (V20-NMP-XADC closes P19 followup NR 0xF9/0xFA stubs per VHDL :7428-7429 Issue2 hard-wire; V20-NMP-02 NR 0x68 read bit 1 leak per VHDL :6093 literal '0' — mask 0xF7→0xF5) + reviewer APPROVE-no-missed (V20-NMP-01 false-positive dismissal confirmed; XADC family fully closed across NR 0xF0/0xF8/0xF9/0xFA). **CPU ~125-130-row table + 1 class-b V20-IM2-01 (pulse-mode CPU /INT polling gap per VHDL :1840 z80_int_n = pulse_int_n AND im2_int_n + im2_peripheral.vhd:186 o_pulse_en — sibling of V19-IM2-04 IM2 mode; CTC/UART INTs silently dropped pre-fix in pulse mode)** + reviewer APPROVE-WITH-NITS (V20R-CPU-NIT-01 persist prev_pulse_int_n_ in save/load schema; V20R-CPU-NIT-02 drop legacy ULA/LINE request_interrupt(0xFF) callbacks — V20-IM2-01 poll captures same events, no double-stamp; V20R-DOC-NIT-01+02 audit-report row-count + line-ref cosmetic) + fix-reviewer APPROVE (architectural cleanup symmetric across pulse/IM2 modes, no missed INTs). **Trend**: P18=10, P19=10, **P20=9**. **Key result**: V20-IM2-01 + V20R-CPU-NIT-02 complete the CPU /INT architecture — single polling path per mode, no legacy callback duplication. XADC family closed. parallax-demo regression remains 33/0/0. Regression suite stable for 2 consecutive passes. |
| 21 | 3 fresh blind (memory skipped); enumeration-table mandate continued; pipelined | **0 class-a, 0 class-b, 4 class-c (1 CPU + 3 NMP) + 3 reviewer NITs (1 doc + 2 code) = 7 effective findings + DivMMC CONVERGED** | DivMMC 108-row table + **0 class-(a/b/c) findings — convergence claim** + 3 class-d pre-existing + 6 sub-class confirmed latent + reviewer APPROVE-no-missed (108/108 row-count match, 10/10 spot-check, all 6 sub-class items verified latent) — **DivMMC + SD + SPI OFFICIALLY CONVERGED, skipped Pass-22+**. NMP 301-row table + 3 class-c (V21-NMP-01 NR 0x03 b7 nr_palette_sub_idx readback hard-wired 0 per VHDL :5894+:5403; V21-NMP-02 CTC port mask 0xF8FF→0xFCFF per ctc.vhd:128-137; V21-NMP-03 NR 0x07 act expbus_eff_en gate per VHDL :5816-5820) + reviewer APPROVE-WITH-NITS (V21R-NMP-NIT-01 doc typo NR 0x28 mis-citation; V21R-NMP-NIT-02 class-c residual — 0x1C3B..0x1F3B alias range should return 0x00 per VHDL OR-fold, not 0xFF default; V21R-NMP-NIT-03 V21-NMP-02-A made discriminative with TC write) + fix-reviewer APPROVE (no missed). CPU ~140-row table + 1 class-c V21-IM2-01 (IM2 fabric int_line_asserted/ack_vector/on_reti/S_ISR→S_0 missing im_mode_==2 gate per VHDL zxnext.vhd:1974 + im2_device.vhd:112,124,150 — spurious request_interrupt(0xFE) in narrow boot window before supervisor ED 5E) + 9 existing tests updated with legitimate ED 5E preconditions or IM=0 override for EXCEPTION-pulse branch testing + reviewer APPROVE-no-missed (gate completeness verified, no enshrinement). **Trend**: P19=10, P20=9, **P21=7** — downtrend continues. Z80N re-audit clean across all 31 opcodes for 2 consecutive passes. ctest 38/38, FUSE 1356/1356, regression 33/0/0. |
| **Total (21 passes)** | | **109 class-(a) + 22 class-(b) + 46 class-(c) + 3 follow-ups; 2 subsystems CONVERGED (Memory P14, DivMMC P21)** | Honestly converging; **8 class-(d) items pending** (no change). DivMMC subsystem skipped Pass-22+. Active: NMP, CPU. |

**Test-coverage retroactive wave (post-pass-10)**: 4 subsystems audited, **105 new regression tests added** (29 memory + 14 divmmc + 45 NMI/MF/Port + 17 CPU). Reviewers found defects/gaps:
- DivMMC: 1 defective (SD-15) + 4 nits → all fixed + reviewed
- NMI/MF/Port: 3 non-discriminative → all made discriminative + reviewed
- CPU: REQUEST-CHANGES (2 non-disc + 6 coverage gaps + 2 misattrib + 1 brittle + 3 disputed subsumes) → 14 findings fixed (17→22 tests) + reviewed (all 8 src/ fixes physically reverted to confirm discriminativeness)
- Memory: 2 non-disc + 5 partial + 4 coverage gaps + 1 weak → 9 findings fixed + reviewed + 1 NIT on FIX-NR5xFF-INT-02 → fixed + re-reviewed

Final state: **all 4 testcov chains fully APPROVED with independent revert-check verification.**

## Class-(d) architectural escalations pending user authorization

| Subsystem | Item | Effort | Boot impact |
|---|---|---|---|
| Memory | port_7ffd_reg vs port_7ffd_dat half-cycle phase | M (CPU half-cycle model required) | None observable above per-instruction granularity |
| Memory | Generic VHDL `*_q` registered signals | M (CPU half-cycle model required) | None observable |
| DivMMC + SD + SPI | SPI cycle-precise FSM + DMA `wait_n` throttle | L-H (multi-subsystem refactor) | None — no boot path needs this fidelity |
| NMI + MF + Port | Stackless NMI (NR $C0 bit 3) | M | Pre-existing Q1 plan cut; not on current boot path |

**Pass-by-pass count by subsystem:**

| Subsystem | P1 | P2 | P3 | P4 | P5 | P6 | P7 | Trend |
|-----------|----|----|----|----|----|----|----|-------|
| Memory | 2 | 2 | 3 | 3 | 2 | 1 | **2** | bumped (integration bugs) |
| DivMMC + SD + SPI | 3 | 0 | 2 | 1 | 2 | 1 | **1** | flat-low |
| NMI + Multiface + Port + NextREG | 5 | 3 | 3 | 9 | 6 | 1 | **3** | bouncing (NR-reset same-shape) |
| CPU (Z80 + Z80N + IM2) | 1+1 | 1 | 3 | 4 | 1 | 1 | **1** | flat-low |
| **Pass total** | **11+1** | **6** | **11** | **17** | **11** | **4** | **7** | |

**Total fixes on integration branch: 69** (12 P1 + 6 P2 + 11 P3 + 17 P4 + 11 P5 + 4 P6 + 7 P7).

**Pattern observation**: pass-7 confirmed the audit is finding **systemic patterns** (e.g., NR-reset preservation; integration-layer wrong-signal wiring; multi-aspect contention paths). Each pass exhausts one pattern only to expose another. Pass-8 will explicitly target the class-(b) backlog under user's stricter convergence criterion.

**Test status (post all-merge, integration branch — Pass-18):**
- `ctest`: **38/38 PASS**
- FUSE Z80 opcode suite: **1356/1356 PASS**
- Full regression suite: **33/0/0** (parallax-demo reference regenerated at commit `67f6218` after user-authorized visual verification — prior 44636-pixel diff was stale reference from micro-timing drift across V14..V18 CPU/IM2/contention fixes, not a functional regression)

**G46(b) crosscheck deferred** (will be run later by the user). High-leverage candidates (across all 3 passes):
- NR $8C cache staleness (memory P2, refined by P3) — bank 0 supervisor wrapper at $007B does `NEXTREG $8C, $80; RET`; fix ensures the next M1 fetch sees the new ROM mapping immediately, with per-slot RAM-mapping preservation.
- Z80N global `tstates` accumulator (CPU P2) — entire Z80N execution path was running at zero global cycles → contention, /INT window, IM2 cadence all corrupted.
- Z80N block-transfer flag updates (CPU P3) — LDIX/LDDX/LDIRX/LDDRX/LDIRSCALE preserved AF entirely, violating VHDL `I_BT='1'` flag cascade.
- INT pulse window machine-awareness (CPU P1 follow-up) — Next-default uses 36-T pulse, not 32.

**Subsystem completeness assessment.** The original Task 2 list (memory, divmmc, nmi, port) was extended to include three additional boot-critical surfaces:

- **CPU (z80_cpu + z80n_ext + im2)** — Z80N opcodes are central to the supervisor (e.g. `NEXTREG $8E,$03` is the bank-3 entry instruction `ED 91 8E 03`); RST/RETN/EX (SP),HL semantics underpin every bank-flip wrapper; IM2 vsync ISR cadence drives every supervisor `EI;HALT;RET` cycle.
- **SD-card pipeline (sd_card + spi)** — DivMMC AUTOMAP plus SD/SPI byte-pump are inseparable for boot. tbblue.fw reads FAT32 via this stack.
- **Multiface (peripheral)** — Wave 1 added; its ROM (loaded from SD) handles NMI + bank flip.

The expanded list (`memory + divmmc/sd/spi + nmi/multiface/port + cpu/z80n/im2`) covers every subsystem the Z80 CPU touches between cold reset and the BASIC welcome screen. No further additions were proposed by any reviewer.

## Findings by subsystem

### Memory (mmu/ram/rom/contention)

Branch: `task2/memory-review` (analysis), `task2/memory-reviewer` (review)
Reports: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS-MEMORY.md`, `…-MEMORY-REVIEW.md`

**Fixes:**

1. **NR $5x,$FF per-slot semantics** (`f832f38`) — VHDL `zxnext.vhd:4607-4699` stores `$FF` verbatim in MMU<i>; arbiter at `:3037-3066` then handles each slot per its own logic. Old jnext fallback (`engage_legacy_ram_paging` for slots 6/7, `map_rom(i,0)` for slots 2-5) silently forced non-$FF mappings, diverging from VHDL. Fix introduces per-slot helper `engage_legacy_rom_paging_slot(i)` (preserves the OTHER ROM slot's prior NR mapping) and uses `set_page(i, 0xFF)` for slots 2-7 (resolves to inactive `sram_pre_active='0'` per VHDL `:3061`).

2. **+3 special-paging arbitration** (`45d8b30`) — VHDL `zxnext.vhd:4623-4684` describes a unified arbiter handling entry/exit/legacy branches. jnext had three independent code paths racing each other on +3 1FFD writes. New `apply_paging_update_()` consolidates them. `port_1ffd_special_old_` model differs from VHDL's per-cycle decay but is functionally equivalent (verified by trace simulation across all 4 special-paging configurations).

**Reviewer note:** A1 reviewer found the merge with the NMI agent's branch would conflict on `src/core/emulator.cpp:1372-1403`. Resolution applied: take memory agent's version (more VHDL-faithful — per-slot helper preserves other slot's mapping; slot 6/7 with `$FF` correctly inactivates per `:3061` rather than forcing `port_7ffd_bank` composition).

**Cross-check vs G46(b):** Both fixes are inert in the current observed boot trace (no NR $52..$57 with $FF; no +3 special paging exercised on `--machine next`). They are latent correctness improvements, not the immediate G46(b) trigger.

### DivMMC + SD-card + SPI

Branch: `task2/divmmc-sd-spi-review` (analysis), `task2/divmmc-sd-spi-reviewer` (review)
Reports: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS-DIVMMC-SD-SPI.md`, `…-DIVMMC-SD-SPI-REVIEW.md`

**Fixes (all in commit `399c9ae`):**

1. **Bug A — NR $BB bit 0 (`automap_nmi_delayed_on` at PC=$0066) was not decoded.** VHDL `zxnext.vhd:2908`. Silent under default $BB=$CD (bit 1 also fires) but a real divergence that surfaces on $BB=$01 configurations.
2. **Bug B — NR $BB bit 7 ($3DXX ROM3 instant_on wildcard) completely missing.** VHDL `zxnext.vhd:2898-2899`. Enabled by default; old tests had "passes vacuously" comments because the path was never reachable. Bug B is the only DivMMC finding with theoretical G46(b) relevance — but EOD-24 trace shows no $3DXX PCs in the active boot trail, so confirmed inert.
3. **Bug C — SPI ports $E7 / $EB had no `port_spi_io_en` gate.** VHDL `zxnext.vhd:2419, 2620-2621` (NR $83 bit 3).

**No fix needed (verified):** SPI byte pipeline VHDL-faithful; SD-card `persistent_response_byte_` + CSD/CID synthesis are documented intentional ZEsarUX-faithful compatibility hacks; automap pipeline collapse is functionally equivalent at byte granularity; port $E3 LSB-decode + mapram OR-latch + NR $09 bit 3 clear + G46(a) RETN delayed clear are correct.

**Reviewer added:** doc nit (NR $BB=$CD bit decomposition prose error in author's report; fix itself is correct), and 4 coverage observations (no unit test for the new SPI port gating; possible follow-up: extract `Emulator::is_spi_io_enabled()` helper).

### NMI + Multiface + Port + NextREG

Branch: `task2/nmi-mf-port-review` (analysis), `task2/nmi-mf-port-reviewer` (review)
Reports: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS-NMI-MF-PORT.md`, `…-NMI-MF-PORT-REVIEW.md`

**Fixes (commit `c1d7998`):**

1. **NMI-1** — `NmiSource` HOLD→END selector ignored ExpBus arm. VHDL `zxnext.vhd:2118`.
2. **NMI-2** — NR 0x02 readback bits 3/2 cleared at `S_NMI_END` instead of "write-back with bit 0" path. Set was missing `nmi_accept_cause` gate. VHDL `:3840-3864`.
3. **NMI-3 (critical)** — `NmiSource` FSM was stuck in `End` forever — `observe_cpu_wr` never wired by Emulator. **Only the first NMI per emulator session ever fired.** Fix auto-advances End→Idle.
4. **NMI-4** — NR 0x02 readback bit 4 (`nr_02_iotrap`, VHDL `:5891+:3885`) was always 0; `nr_da_iotrap_cause_` not composed.
5. **NR-2** — NR 0x52..0x55 `$FF` writes silently remapped to physical page 0 via `map_rom(i,0)`. Now stores `$FF` verbatim — matches VHDL `mmu_A21_A13(8)='1'` → `sram_pre_active=0` (slot inactive).

**Reviewer added 2 new findings (R-1, R-2)**, not yet fixed (deferred):
- R-1: `nr_02_pending_*` readback latches reset only on `i_RESET` per VHDL `:1730`, not on every config-mode write.
- R-2: `nr_da_iotrap_cause_` SET at `emulator.cpp:2370/2382/2392` lacks the VHDL `:3871` `nmi_accept_cause` gate.

**Cross-check vs G46(b):** None of the NMI/MF/Port findings are on the G46(b) slide-cascade critical chain (which is rooted at `NEXTREG $8E,$03` paging per EOD-24). NMI-3 is a load-bearing correctness fix for any future NMI-driven workflow but doesn't change the current boot.

### CPU (Z80 + Z80N + IM2)

Branch: `task2/cpu-z80n-im2-review` (analysis), `task2/cpu-z80n-im2-reviewer` (review), `task2/cpu-int-pulse-fix` (follow-up fix)
Reports: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS-CPU.md`, `…-CPU-REVIEW.md`

**Primary fix (commit `65b5918`) — Z80N T-state systematic undercount.** `Z80Cpu::execute()` raw-reads ED + Z80N opcode bytes, bypassing FUSE timing callbacks. `execute_z80n()` was returning the inner-opcode T-state count without including the ED-prefix M1 fetch. Most Z80N opcodes were 4 T-states short; block-ops 8 short on non-terminal iterations. Fixed every opcode to match the published Spectrum Next timing table:

| Opcode group | Old | New |
|---|---|---|
| SWAPNIB / MIRROR / BSx / MUL / ADD HL/DE/BC,A / PIXELDN / PIXELAD / SETAE | 4 | 8 |
| TEST n | 7 | 11 |
| ADD HL/DE/BC,nn | 12 | 16 |
| OUTINB | 10 | 16 |
| NEXTREG nn,A | 13 | 17 |
| NEXTREG nn,nn | 16 | 20 |
| JP (C) | 12 | 13 |
| LDIX / LDDX | 13 | 16 |
| LDIRX / LDDRX / LDPIRX / LDIRSCALE | 16 | 21 (repeating) / 16 (terminal) |
| PUSH nn | 23 | 23 (already correct) |
| LDWS | 14 | 14 (already correct) |

Rationale: NextZXOS supervisor invokes NEXTREG/MUL/ADD HL,A heavily; the bank-3 wrapper at $5B48 is literally `ED 91 8E 03 C9`. The undercount drifted IM2 vsync timing, contention windows, and deferred-NR-write boundaries earlier than VHDL would emit them.

**Top G46(b) suspect — PUSH imm (ED 8A) byte order — VERIFIED CORRECT** via three independent paths:
1. VHDL trace `t80n_mcode.vhd:1921-1948` — first operand byte → high stack address (`mem[SP+1]`); second operand byte → low (`mem[SP]`).
2. Test fixture `tests.expected:281-301` — `ED 8A 12 34` → `mem[$FFEE]=$34, mem[$FFEF]=$12`.
3. Z80 stack semantics — `mem[SP]=low, mem[SP+1]=high` after PUSH.

The C++ implementation matches all three sources. **The G46(b) "3 missing PUSHes" divergence is NOT in PUSH imm semantics.** This is a load-bearing negative result for the G46(b) investigation.

**Other negative results (re-verified by reviewer):** NEXTREG via Z80N opcode correctly converges with port-write semantics through the same `nextreg_.select() / .write_selected()` and the `defer_cpu_nr_writes_` window. JP (C) PC bit composition preserves PC[15:14]. LDIX/LDDX/LDIRX/LDDRX/LDPIRX/LDIRSCALE direction matches VHDL. IM2 fabric (daisy-chain priority, IEO propagation, S_REQ→S_ACK, S_ISR→S_0 RETI gating, pulse-mode width latch) faithful per `im2_control.vhd` / `im2_device.vhd` / `im2_peripheral.vhd`.

**Follow-up fix (commit `3c89104`) — INT pulse window now machine-aware.** Reviewer flagged `Z80Cpu::INT_PULSE_TSTATES = 32` as hardcoded, but VHDL `zxnext.vhd:2017-2033` specifies:
- 32 T-states for **48K and +3** (`machine_timing_48` / `machine_timing_p3` set)
- **36 T-states for 128K, Pentagon, and Next-default** (rest)

The G46(b) test machine is `next` (ZXN_ISSUE2), so the prior 32-cycle constant was wrong by 4 cycles. Fix wires `Z80Cpu::set_machine_type()` from `Emulator::reset_machine`, the NR $03 runtime write handler, and post-`load_state` (mirrors the existing `Im2Controller::set_machine_timing_48_or_p3` pattern). New unit test `cpu_int_pulse_test` adds 10 cases covering the boundary at delta=32/33 (48K/+3) and delta=33/36/37 (128K/Pent/Next), plus the 4-T-state divergence band where the prior hardcoded 32 was wrong.

**This is the only finding from Task 2 with potential G46(b) relevance.** Whether it actually shifts the supervisor's "between RST $08 hits #2 and #3" stack ascent is unconfirmed — worth re-running the G46(b) cycle on this branch to compare.

**Reviewer-added findings (3 nits, not blocking):**
- 3 1-off T-state values in `tests.expected` fixtures (test-fixture nit, not emulator).
- BRLC UB rationale could be more explicit in code comment.
- INT pulse window finding (now fixed in `3c89104`).

## Cross-finding: memory ↔ NMI agent slot 2-5/6/7 $FF semantics

Two agents independently arrived at the same VHDL-faithful semantics for slot 2-7 with `$FF`: store `$FF` verbatim → `Mmu::set_page(i, 0xFF)` → `nr_mmu_=0xFF`, `slots_=0xFF`, `read_only_=false` → `Mmu::rebuild_ptr` nullifies pointers → reads return `$FF` (floating bus), writes dropped. This matches VHDL `mmu_A21_A13(8)='1'` → `sram_pre_active='0'`.

**However, the two branches conflicted at merge** (both edited `src/core/emulator.cpp:1372-1403`). The NMI agent's version used `engage_legacy_ram_paging()` for slots 6/7 — A1 reviewer flagged this as the **wrong VHDL branch** (it would force `port_7ffd_bank` composition rather than inactivating the slot). The memory agent's version (per-slot ROM helper for 0/1, `set_page($FF)` for 2-7) was retained per the reviewer's recommendation. NMI agent's other fixes (NMI-1..4) merged cleanly.

## G46(b) cross-check (first pass)

First-pass per-subsystem reviewer assessments were unanimous: **none of the eleven first-pass fixes is on the G46(b) slide-cascade critical chain.** The G46(b) bug is supervisor-stack divergence between RST $08 hits #2 and #3 (3 missing PUSHes / 3 extra POPs vs CSpect; per memory `project_g46b_2026_05_09_eod24_nr8e03.md`).

The first-pass finding most-plausibly relevant was **INT pulse window machine-awareness** (CPU follow-up fix `3c89104`): the supervisor's `EI;HALT;RET` pattern depends on IM2 vsync edges arriving with the correct cadence; a 4-cycle wrong pulse width could shift boundary-case ISR firings.

The verification pass added two more high-leverage candidates (see below).

---

# Verification re-audit (second pass, blind)

After the first-pass merge closed, four fresh agents (one per subsystem) were launched off integration-branch HEAD `e6bd9ce`, all in ultrathink mode, **forbidden from reading any first-pass `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS-*.md` file**. Each was told to audit the post-fix state against VHDL from scratch and to actively hunt for what the first pass missed. Per-subsystem verification reports: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY-{MEMORY,DIVMMC-SD-SPI,NMI-MF-PORT,CPU}.md`.

The verification agents found **6 additional class-(a) bugs** the first-pass missed:

## Memory verification (branch `task2/verify-memory`, HEAD `3dd4e73`)

1. **NR $8C lock-bit cache staleness** — VHDL `zxnext.vhd:2981-3008` is combinational: `altrom_lock_rom1/rom0` bits feed `sram_rom` immediately. Pre-fix C++ `set_nr_8c` only stored the byte, leaving cached `read_ptr_[0]` and `read_ptr_[1]` **stale until the next paging-port write**. Fix calls `apply_legacy_rom_slots_()` from `set_nr_8c`. **High G46(b) candidate**: the bank 0 supervisor wrapper at `$0066-$007F` ends with `NEXTREG $8C, $80; RET` (ALTROM enable). Pre-fix, the next M1 fetch in slot 0/1 reads from the stale ROM mapping, executing the wrong code — exactly the pattern of "wrong bank visible at $3F00" described in EOD-23.

2. **NR $50/$51 dispatch with v in `[$E0..$FE]`** — Per VHDL `:2964 + :3037-3057`, ANY logical page ≥ $E0 on slot 0/1 has `mmu_A21_A13(8)='1'` and falls through to legacy ROM (sram_rom-derived) — the same effect as the canonical `$FF` sentinel. The first pass only handled the exact `$FF` value; pre-fix C++ stored other `$E0..$FE` values verbatim and used `(v + $20) mod 256` → mis-routed slot 0/1 to wrong-aliased RAM (e.g. `$E5` → SRAM page `$05`). Fix extends the `page >= 0xE0` gate in `Mmu::rebuild_ptr` to cover the full range, honouring `port_eff7_reg_3` (RAM-at-$0000).

**Class-(b) reported, not fixed:**
- NR $08 readback returns shadow not effective (`emulator.cpp:3127`); should consult `eff_nr_08_contention_disable` per VHDL `:5906`.
- Layer 2 `port_123b` segment-mask gates slot 0/1 differently from VHDL.

## DivMMC + SD + SPI verification (branch `task2/verify-divmmc-sd-spi`, HEAD `3d61e3f`)

**0 class-(a) bugs found.** One class-(b) modelling gap reported (no code change applied):

- `DivMmc::set_rom3_active(mmu_.rom3_selected())` is fed at three sites in `emulator.cpp` (`:2335,:2424,:3785`) using `rom3_selected() = (port_1ffd(2) AND port_7ffd(4))`. But VHDL `sram_divmmc_automap_rom3_en` (`:3138`) consumes `sram_rom3` which on ZXN/128K mode without altrom lock is `port_7ffd(4)` **alone** (per `:2997-3007`) — bank 1 also lights it. The correct formula already exists as `Mmu::sram_rom3()` but isn't wired into the DivMMC feeder. Boot impact: nil (NextZXOS uses bank 0/3 only); could affect tape-trap paths on 128K/+3 in bank 1.

Class-(c) observations: SPI no-device read doesn't drift `rx_data_` to $FF; CMD17/18 OOR returns R1=$00 instead of spec $20; VHDL `*_q` half-cycle delay collapsed (documented intentional simplification).

## NMI + Multiface + Port verification (branch `task2/verify-nmi-mf-port`, HEAD `78f5f1c`)

3. **F9 hotkey bypassed NmiSource arbitration** — `Emulator::on_hotkey_f9_mf_nmi()` called `multiface_.button_press()` directly, bypassing NR $06 bit 3, port_e3_reg(7) (CONMEM), divmmc_nmi_hold, and nr_03_config_mode gates. Per VHDL `:4290`, the Multiface entity's `button_i` is wired to `nmi_mf_button` (= arbiter-elected MF strobe), NOT raw `hotkey_m1`. Fix routes through `nmi_source_.mf_button_strobe()`.

4. **NR 0x02 readback bits 3/2 wrongly cleared on config_mode entry** — VHDL `:3840-3864` puts these readback latches in independent clocked processes whose clear cascade is reset OR explicit-bit-write only — config_mode is NOT in either cascade. (This was the deferred R-1 finding from the first-pass NMI reviewer; verification pass actually applied the fix.)

5. **`/NMI` line wrongly low through `S_NMI_HOLD`** — VHDL `:2168` asserts `/NMI` only in IDLE+activated, FETCH, or expbus-debounce, NOT in HOLD. Functionally invisible (Z80 NMI is edge-triggered) but a spec violation.

**Class-(b) reported, not fixed:**
- NR 0x02 readback bit 7 (`nr_02_bus_reset`) not modelled.
- `nr_d9_iotrap_write_` and `nr_da_iotrap_cause_` updated without the `nmi_accept_cause` gate (VHDL `:3870-3892`). (This was deferred R-2 from the first-pass NMI reviewer; still deferred.)

**Audit-prompt corrections** (the second-pass agent caught two errors in my prompt brief): NR $A2/$A3 are I2S audio (`nr_a2_pi_i2s_ctl`), **NOT** Multiface — Multiface enable is NR $83 bit 1 (`port_multiface_io_en`), wired correctly at `emulator.cpp:1951`. NR $C5 is CTC interrupt-enable, **NOT** DivMMC NMI.

## CPU (Z80 + Z80N + IM2) verification (branch `task2/verify-cpu-z80n-im2`, HEAD `86128d5`)

6. **Z80N global `tstates` counter never incremented** — Z80N opcodes bypass `fuse_z80_execute_one()` and use raw `MemoryInterface::read()` for both M1 fetches and operand reads, **so they never increment FUSE's global `tstates` counter**. The first-pass A4 fix corrected the **return value** of `execute_z80n()` (correct T-states reported per opcode), but didn't notice the global counter was never updated. **The entire Z80N execution path consumed zero global cycles.** Fix: `tstates += t` after Z80N execution. **High G46(b) candidate**: this affects contention `(hc, vc)` derivation, the `/INT` pulse-window expiry check, and IM2 vsync cadence. Bank 3 wrapper at $5B48 (`ED 91 8E 03 C9`) is hit constantly per cycle in jnext (per memory); each hit consumed 0 global cycles instead of ≥16. Likely causes premature/spurious INT firings — verifying agent specifically flags: "3 spurious INT events would explain exactly 6 bytes of stack growth" between RST $08 hits #2 and #3.

**Class-(b) reported, not fixed:**
- M1-fetch contention skipped on Z80N path's raw reads (refactor would be invasive).
- `Im2Controller::ack_vector()` advances S_REQ → S_ACK before `fuse_z80_interrupt` may reject during EI grace period (rare; doesn't affect IM1-using boot path).

**Negative result re-confirmed:** PUSH imm (`ED 8A`) byte order **independently re-verified correct** against `t80n_mcode.vhd:1921-1948`, the `ed8a_basic`/`ed8a_ffff`/`ed8a_preserve` test fixtures, and Z80 stack semantics. `ED 8A NH NL` is big-endian in the instruction stream, pushed as `mem[SP+1]=NH, mem[SP]=NL`. Two independent ultrathink agents have now confirmed this — definitively rules out PUSH imm byte order as a G46(b) cause.

## Cross-finding: memory ↔ NMI agent slot 2-5/6/7 $FF semantics (first pass)

Two first-pass agents independently arrived at the same VHDL-faithful semantics for slot 2-7 with `$FF`: store `$FF` verbatim → `Mmu::set_page(i, 0xFF)` → `nr_mmu_=0xFF`, `slots_=0xFF`, `read_only_=false` → `Mmu::rebuild_ptr` nullifies pointers → reads return `$FF`, writes dropped. Matches VHDL `mmu_A21_A13(8)='1'` → `sram_pre_active='0'`.

The two branches conflicted at merge (both edited `src/core/emulator.cpp:1372-1403`). Memory agent's version (per-slot ROM helper for 0/1, `set_page($FF)` for 2-7) was retained per A1 reviewer's analysis. The verification pass (A1') generalized this further: extending the gate to cover the full `$E0..$FE` range, not just `$FF`.

---

# Pass-3 verification re-audit (third pass, blind to passes 1+2)

After the second-pass merge closed (HEAD `7747202`), four fresh agents were launched off integration-branch HEAD with the same blind constraint extended to BOTH prior passes' reports. Pass-3 prompts emphasized **methodology shifts**: edge-case / boundary inputs, differential audit direction (VHDL→C++), cold/warm/soft reset interactions, power-on defaults sweep, multi-state interaction edges, and CPU-specific R-register / MEMPTR / Q register / save-load coverage.

Per-subsystem pass-3 reports: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY3-{MEMORY,DIVMMC-SD-SPI,NMI-MF-PORT,CPU}.md`.

**Pass-3 found 11 additional class-(a) bugs.** Pattern observation: many pass-3 findings are either **refinements of pass-2 fixes** that were too aggressive (memory) or **previously-class-(b) findings promoted to class-(a) and finally fixed** (NMI/MF/Port).

## Memory pass-3 (branch `task2/verify3-memory`, HEAD `31d1786`)

7. **`unlock_paging()` left `port_7ffd_` bit 5 stale** — VHDL `:3654-3656` clears `port_7ffd_reg(5)` directly on NR $08 bit 7. C++ only cleared the standalone `paging_locked_` flag. Affects MF+3 readback at $7xxx (VHDL `:4318`) and Pentagon-1024 bank-5 composition (VHDL `:3765`). Fix in `mmu.h`.

8. **`set_nr_8c()` clobbered explicit RAM mappings on slots 0/1** — Pass 2's fix to call `apply_legacy_rom_slots_()` from `set_nr_8c` was **too aggressive**: it forced slots 0/1 to ROM, breaking the canonical `NR $50, RAMpage; NR $8C, …` pattern. VHDL doesn't pulse `port_memory_change_dly` on NR $8C (per `:3813`). Pass-3 fix: per-slot `read_only_[i]==true` gating — only rebuild slots currently mapped as ROM.

9. **`engage_legacy_rom_paging_slot()` under EFF7=1 broke NR $50/$51 read-back** — Pass 2's helper called `set_page(slot, 0)` setting `nr_mmu_[slot]=0x00`. But VHDL `nr_mmu_we` stores `nr_wr_dat` verbatim; eff7's RAM-at-$0000 override fires only on `port_memory_change_dly='1'`. Pass-3 fix routes the cached pointer to RAM page 0/1 while keeping `nr_mmu_[slot]=0xFF`.

## DivMMC + SD + SPI pass-3 (branch `task2/verify3-divmmc-sd-spi`, HEAD `d54a053`)

10. **DivMMC `button_nmi` strobe gating** — Emulator must check `divmmc_.is_enabled()` before forwarding `NmiSource::divmmc_button_strobe`. VHDL `i_automap_reset` (= `port_divmmc_io_en=0 OR nr_0a_divmmc_automap_en=0`) holds the FF at '0' when DivMMC disabled. NmiSource only gated on NR $06 bit 4 alone. Fixed at both call sites.

11. **SPI master $FF default-else** — `write_data`/`read_data` with no active device now force `rx_data_=$FF` to match VHDL `spi_miso <= '1'` fallback (`zxnext.vhd:3278-3280`). (Pass 2 had this as class-(c); pass 3 promoted to class-(a).)

**Cross-subsystem catch (out-of-scope but flagged)**: NR $09 bit 3 incorrectly drives `sprites_.set_over_border()` in `emulator.cpp:3138` — sprite-over-border lives at NR $15 bit 1. NR $09 bit 3 should only clear mapram per VHDL. Not fixed (out of subsystem); flagged for follow-up.

## NMI + Multiface + Port pass-3 (branch `task2/verify3-nmi-mf-port`, HEAD `d841887`)

12. **NR $02 bit 7 (`nr_02_bus_reset`) latch missing** — VHDL `:5119` captures bit 7 on every NR $02 write; `:5891` surfaces on readback. C++ hard-coded readback bit 7 to zero. Fix added `Emulator::nr_02_bus_reset_` flag plus capture/surface logic. (Pass 2 had this as class-(b); pass 3 fixed.)

13. **NR $0A bits 7:6 (`nr_0a_mf_type`) readback ignored config_mode gate** — VHDL `:5191-5198` only commits mf_type when `nr_03_config_mode='1'`; C++ read sourced from raw cached last-written byte. Fix sources from `multiface_.mf_type()` (authoritative gated state).

14. **NR $DA / NR $D9 iotrap event capture missing `nmi_accept_cause` gate** — VHDL `:3871` and `:3892` gate both fields on `nmi_state = IDLE OR FETCH`; C++ updated unconditionally. Fix added `Emulator::nmi_accept_cause_()` helper. (This was the deferred R-2 finding from pass 1, still flagged class-(b) in pass 2; pass 3 finally fixed it.)

**Honest coverage caveat from pass-3 NMI agent**: only audited ~20% of the total NR surface (NMI + iotrap + MF FSM + key NR readbacks). **Not audited**: palette $40-$44, copper $60-$63, sprite $34-$3F, line int $22-$23, audio $26-$2E + DMA $C8-$CF int routing. Agent recommends pass-4 because "the bug pattern (gate-omission in readback / event capture) likely repeats in unaudited areas."

## CPU pass-3 (branch `task2/verify3-cpu-z80n-im2`, HEAD `0a64eff`)

15. **Z80N block-transfer flag updates missing** (LDIX / LDDX / LDIRX / LDDRX / LDIRSCALE) — VHDL `I_BT='1'` block-transfer flag pulse cascades F.X = ALU_Q[3], F.Y = ALU_Q[1], F.H=0, F.N=0, F.P=(BC≠0). C++ preserved AF entirely. Fix via new `ldi_family_flags()` helper. LDPIRX deferred class-(b) (its VHDL ALU operands are commented out, leaving flag composition ambiguous).

16. **Z80N flag-writing opcodes did not update Q** — Q is the F-assembly shadow used by FUSE's SCF/CCF undocumented X/Y flag composition (via `last_Q ^ F`). TEST_N, ADD_HL_A, ADD_DE_A, ADD_BC_A, LDWS, and the LDIX-family now all assign `Q = f` after writing F.

17. **`Z80Cpu::save_state` / `load_state` missing MEMPTR + Q** — both are first-class fields of `Z80Registers` (synced from FUSE) but weren't serialised. Fix: symmetric `write_u16(MEMPTR); write_u8(Q)` pair.

z80n_test went from 80/85 → 85/85 with five LDI-family fixtures updated to spec-compliant flag composition.

## Convergence assessment after 3 passes

The audit has **not fully converged**. Pass 3 found 11 new class-(a) bugs at a similar rate to passes 1 and 2, contradicting the naive "diminishing returns" expectation. Why:

1. Each pass had a different methodology focus, exposing different bug classes.
2. Some pass-2 fixes were too aggressive in multi-state contexts; pass 3 caught the regressions/refinements.
3. Many class-(b) findings from passes 1-2 were correctly promotable to class-(a) once examined more carefully — pass 3 promoted three of them.
4. Pass 3 acknowledges incomplete NR-surface coverage (~20% in the NMI subsystem) and predicts gate-omission bugs in the unaudited 80%.

**A pass 4 would likely find more class-(a) bugs**, particularly across the unaudited NR clusters (palette $40-$44, copper $60-$63, sprite $34-$3F, line int $22-$23, audio $26-$2E, DMA int $C8-$CF). Returns are no longer rapid in any single area but the surface area is large.

For the goal "verify that no new changes are detected", the answer after 3 passes is **no — new bugs are still being found at each pass.** The audit is approaching but has not reached convergence on the surfaces deeply audited; it is far from convergence on the broader NR surface.

## G46(b) cross-check (aggregate, all 3 passes)

The highest-leverage candidates remain (all from passes 2-3):

- **NR $8C cache staleness + per-slot read-only gate** (memory pass-2 #1, refined by pass-3 #2). Bank 0 supervisor wrapper `NEXTREG $8C, $80; RET` now updates cached read pointers immediately, without clobbering RAM mappings on slots 0/1.
- **Z80N global `tstates` accumulator** (CPU pass-2 #6). Z80N path no longer at zero global cycle cost.
- **Z80N block-transfer flag updates** (CPU pass-3 #15). LDIX/LDDX/etc. now set X/Y/H/N/P per VHDL — affects supervisor's flag-driven control flow during memory copies.
- **INT pulse window machine-awareness** (CPU pass-1 follow-up). Next-default uses 36-T pulse, not 32.

Either or any combination could plausibly contribute to the G46(b) supervisor stack divergence between RST $08 hits #2 and #3. **G46(b) cycle crosscheck deferred to user** on the post-pass-3-merge integration branch.

PUSH imm byte order **independently re-confirmed correct** by both pass-2 and pass-3 CPU agents — definitively rules out PUSH imm as a G46(b) cause.

---

# Pass-4 verification re-audit (fourth pass, blind, sharper methodology)

After pass-3 merge closed, pass-4 launched with explicit methodology shifts per agent:

- **Memory**: transition-edge stale-cache table, reset-state matrix, save/load full coverage, Pentagon × EFF7 cross-products, differential VHDL signal coverage
- **DivMMC + SD + SPI**: SD-card lifecycle coherence, CRC paths, soft-reset matrix, boundary block addresses, multi-CMD55 retry
- **NMI + MF + Port**: **the 80% sweep** — palette $40-$44, copper $60-$63, sprite $34-$3F, line int $22-$23, audio $26-$2E + $84-$89, DMA int $C8-$CF, UART $98-$9C+$E0-$EF, general I/O $A0-$A1, general config $05-$0A
- **CPU**: Z80N + DD/FD/CB prefix composition, R/MEMPTR/Q semantics, contention-stress, FUSE-internal state inventory, IM2 stress cases, NMI/INT race, HALT corners

Per-subsystem pass-4 reports: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY4-{MEMORY,DIVMMC-SD-SPI,NMI-MF-PORT,CPU}.md`.

## Memory pass-4 (branch `task2/verify4-memory`)

10. **EFF7(3) RAM-at-$0000 over-applied on NR $50/$51 = $FF writes** — Pass-3 fixed `engage_legacy_rom_paging_slot` under EFF7=1, but pass-4 found EFF7 was still being over-applied via a different path. VHDL `:4636-4644` fires only on `port_memory_change_dly='1'`, NOT on `nr_mmu_we`. Fix: scoped EFF7 application in `engage_legacy_rom_paging_slot()` and `rebuild_ptr()` slot 0/1 high-page branch.

11. **`nr_mmu_[8]` not persisted in save/load** — Pass-2 added the verbatim $E0..$FE storage but the save/load round-trip lost it. Fix: appended `write_bytes(nr_mmu_, 8)` / `read_bytes(nr_mmu_, 8)`.

12. **NR $12 (Layer 2 active bank) write didn't propagate to `Mmu::l2_bank_`** — VHDL `:2968` is combinational; CPU L2 map used stale bank between port $123B writes. NR $13 had correct propagation; NR $12 was the missing seam. **Cross-subsystem catch** (NR $12 lives in video/Layer 2 but the MMU mapping needs it). Fix: added `Mmu::set_l2_active_bank()` and called from NR $12 handler.

## DivMMC + SD + SPI pass-4 (branch `task2/verify4-divmmc-sd-spi`)

13. **CMD24 RECEIVING_DATA mishandles $FF gap bytes** — Per SD Physical Layer Simplified Spec 6.00 § 7.3.3.2, card MUST tolerate any number of $FF "host SPI clock" gap bytes between R1 response and the $FE start-of-data token. Pre-fix absorbed each $FF into `data_block_[data_idx_++]`, shifting 512-byte payload by one byte → corrupted disk write. Boot path unaffected (tbblue.fw doesn't gap-pad), but esxdos F_WRITE / FatFs DO send gap bytes per spec.

**Honesty note from pass-4 DivMMC agent**: two of my pass-4 prompt audit-guidance bullets were factually wrong (SPI clock dividers NR $86/$87/$88 and port $EF mirror don't exist in VHDL). Agent correctly did NOT fabricate findings to satisfy them — exactly the unbiased posture the user requested.

## NMI + Multiface + Port pass-4 (branch `task2/verify4-nmi-mf-port`) — the 80% sweep

This was largely **greenfield surface** (NMI subsystem agent in pass-3 explicitly flagged ~80% unaudited at depth). Found 9 class-(a) bugs:

14. **NR $04** — Issue-2 board mask `v & 0x7F` (zxnext.vhd:5717).
15. **NR $11** — config_mode gate + Issue-2 bit-0-only capture (zxnext.vhd:5208-5217). **Most boot-relevant** of the pass-4 NMI fixes (gate omission could let mid-flight config bleed timing changes).
16. **NR $2F** — write mask `v & 0x03` (zxnext.vhd:5331).
17. **NR $44** — composed `read_9bit()` (zxnext.vhd:6047-6048); required priority storage for ALL palette types (palette_utm + palette_l2s store priority for ULA/L2/sprite/tilemap). PAL-05 unit test was actually FAILING pre-fix on the post-pass-3 main; **no prior pass caught it**.
18. **NR $8A** — write mask `v & 0x3F` (zxnext.vhd:5525).
19. **NR $90/$93** — Pi GPIO output enable masks (zxnext.vhd:5537, 5546).
20. **NR $98-$9B** — Pi GPIO input semantics: return 0 (not write shadow).
21. **NR $A8** — ESP GPIO0 enable mask `v & 0x01` (zxnext.vhd:5570).
22. **NR $A9** — ESP GPIO0 input semantics: return 0.

Pass-4 NMI agent reports **~70% deep audit in this pass**; still deferred: Cluster C (sprite mirror $34/$35-$39/$75-$79), UART $D0-$EF, cpu_speed misc $F0-$FF.

Open question worth pass-5: **NR $86/$87/$88 reset-type gating** (VHDL `:5061-5067` honors `nr_89_bus_port_reset_type`; jnext applies unconditionally per `nextreg.cpp:69-71` "approximation"). Same shape as the NR $82-$84 fix landed in earlier waves.

## CPU pass-4 (branch `task2/verify4-cpu-z80n-im2`)

Mostly **duals of pass-3 fixes**:

23. **Z80N `Q = 0` hygiene missing for non-F-writing opcodes** — Pass-3 added Q updates for F-writing opcodes; pass-4 caught the dual. SWAPNIB, MIRROR, MUL, BSLA/BSRA/BSRL/BSRF/BRLC, ADD_*_NN, PUSH_NN, OUTINB, NEXTREG_*, PIXELDN/AD, SETAE, JP_C, LDPIRX, LOOP all bypassed FUSE's "Q=0 at top of every opcode" contract. Subsequent SCF/CCF would read stale Q → wrong undocumented X/Y bits. Fix: `z80.q = 0` at top of Z80N dispatch.

24. **Z80N `iff2_read = 0` hygiene missing** — Z80N between `LD A,I` and INT-acceptance left `iff2_read` set, firing the NMOS quirk on a non-immediate boundary. Fix: `z80.iff2_read = 0` at top of Z80N dispatch.

25. **Save/load gap for FUSE-internal `interrupts_enabled_at` + `iff2_read`** — Pass-3 added MEMPTR + Q to save/load; pass-4 found two more FUSE-internal fields that affect runtime but weren't persisted.

26. **`ADD HL/DE/BC, nn` didn't update MEMPTR** — VHDL `t80n_mcode.vhd:1872-1878` sets LDZ@MCycle2 + LDW@MCycle3 ⇒ end-state `WZ = nn`. Fix: `regs.MEMPTR = nn`. Pre-existing test fixtures with `MEMPTR=0000` were CORRECTED to match VHDL — test was wrong, code was wrong-but-matching-test-bug.

## Convergence assessment after 4 passes

**Audit has NOT converged.** Pass 4 found 17 new class-(a) bugs — actually MORE than pass 3.

Subsystem-specific trends:
- **DivMMC+SD+SPI**: P1=3 → P2=0 → P3=2 → P4=1. Converging.
- **Memory**: P1=2 → P2=2 → P3=3 → P4=3. Flat / not converging.
- **NMI+MF+Port**: P1=5 → P2=3 → P3=3 → P4=9. Rising, due to greenfield 80% sweep.
- **CPU+Z80N+IM2**: P1=1+1 → P2=1 → P3=3 → P4=4. Rising slightly, due to dual-finding pattern.

**Per the user's standing instruction** — "iterate until reviews honestly converge to no class-(a) bugs" — pass 5 is mandatory. Methodology focus for pass 5:

- **NMI+MF+Port**: cluster C sprite mirror ($34/$35-$39/$75-$79) + UART $D0-$EF + cpu_speed misc $F0-$FF + NR $86/$87/$88 reset-type gating
- **CPU**: continue dual-finding hunt (any other FUSE-internal state? any other dual-of-existing-fix?), examine LDPIRX flag composition definitively, contention-stress quantification
- **Memory**: the EFF7 cluster has now seen 3 distinct passes find new aspects; still not converged
- **DivMMC**: focus on the boot path that's actually exercised — does the FAT32 read path have any subtle bugs

## G46(b) cross-check (aggregate after 4 passes)

Highest-leverage candidates for G46(b) supervisor stack divergence:

- **NR $8C cache + per-slot read-only** (memory P2+P3+P4 cluster). Now extensively audited; bank 0 wrapper at $007B does `NEXTREG $8C, $80; RET`; cache rebuild + RAM mapping preservation.
- **Z80N global `tstates`** (CPU P2). Path no longer at zero global cycle cost.
- **Z80N block-transfer flags + Q hygiene + iff2_read hygiene** (CPU P3+P4). LDIX/LDDX/etc. now spec-compliant; Q=0 at top of every Z80N op.
- **Z80N `ADD HL/DE/BC, nn` MEMPTR** (CPU P4). Supervisor MEMPTR fingerprint now correct.
- **NR $11 config_mode gate** (NMI P4). Issue-2-board-id gate; could let mid-flight config bleed.
- **NR $44 priority composition** (palette P4). Mid-pipeline palette state.
- **INT pulse window machine-aware** (CPU P1 follow-up).

PUSH imm byte order independently re-confirmed correct by pass-2 + pass-3 + (implicitly) pass-4 CPU agents — definitively not the cause.

G46(b) cycle re-run remains deferred to user.

## Open questions / deferred work (post-pass-4)

1. **Pass 5** (mandatory per user's standing instruction).
2. **G46(b) cycle re-run** on integration branch.
3. **Cross-subsystem class-(a) flagged by P3 DivMMC**: NR $09 bit 3 incorrectly drives `sprites_.set_over_border()` (should be NR $15 bit 1). Still not fixed.
4. **NMI 80%-sweep deferred clusters**: sprite mirror $34/$35-$39/$75-$79; UART $D0-$EF; cpu_speed misc $F0-$FF; NR $86/$87/$88 reset-type gating.
5. **LDPIRX flag composition** (CPU class-b) — VHDL ALU operands commented out.
6. **Memory class-(b) #1**: NR $08 readback returns shadow not effective.
7. **Memory class-(b) #2**: Layer 2 `port_123b` segment-mask asymmetry.
8. **DivMMC class-(b)**: `rom3_selected()` vs `sram_rom3()` mismatch.
9. **CPU class-(b) #1**: M1-fetch contention skipped on Z80N raw-read path.
10. **CPU class-(b) #2**: `Im2Controller::ack_vector()` early state advance.
11. **CPU class-(b) #3**: DD/FD/CB inner-byte delivery to IM2 decoder FSM.
12. **Test-coverage gaps** — multiple. See per-subsystem reports.
13. **`port_1ffd_special_old_` decay model** — functionally equivalent approximation.
14. **`StateReader::read_u8()`** lacks bounds check — pre-existing.

## Test status (final, integration branch, post Pass-17)

```
ctest                              38/38 PASS  (Release build)
fuse_z80                       1356/1356 PASS
rewind_test (unit)                 22/0/10s  (PASS)
test/00regression/regression       (rewind-func regex now resilient to test-count growth — Pass-16 follow-up #1 closed at integration HEAD via commit e846645)
```

## Branch state

```
Branch: nextzxos-boot-subsystem-analysis (off main)
Total fixes through Pass-17: ~109 class-(a) + 13 class-(b) + 28 class-(c) + 1 follow-up + 1 build fix
Pushed: NO
Integration HEAD (Pass-17): 1d16964
```

## Convergence status (per Pass-17)

| Subsystem | Status | Last finding |
|-----------|--------|--------------|
| **Memory** | **CONVERGED** (skipped Pass-15..17) | V13-MEM-01 (Pass-13); Pass-14 audit ZERO + reviewer APPROVE |
| DivMMC + SD + SPI | NOT converged | V17-DIVMMC-01 (ACMD41 HCS not reflected in CMD58 OCR CCS) — close to convergence: 1 class-c only |
| NMI + MF + Port + NextREG | NOT converged | V17-NMP-01 (NR $B8-$BB readback defaults), V17-NMP-02 (port $FE decode), V17-NMP-03 (port $FF decode) |
| CPU + Z80N + IM2 | NOT converged | V17-CPU-01 (IM2 pulse-mode latch — high-impact class-b); V17-Z80N-01a/b + V17-CPU-NIT-04 (Z80N strict-UB-free shifts — full BSLA/BSRF/BSRA family closed) |

## Pass-12 details

**12 fixes total, all merged with discriminative regression tests + independent fix-reviewer APPROVE:**

### Findings table

| ID | Subsystem | Class | Summary | Origin | Commit (in-chain) |
|----|-----------|-------|---------|--------|-------------------|
| V12-MEM-01 | Memory | b | NR $8C and `set_machine_type` clobbered `nr_mmu_[]` to 0xFF, dropping verbatim NR $50/$51 values in 0xE0..0xFE; VHDL :3813, :4607 leaves MMU<i> untouched | audit | ce1d3be |
| V12-MEM-02 | Memory | b | ContentionModel state (cpu_speed, contention_disable, port_7ffd_io_en) reverted to constructor defaults on load_state — broke NR $08 read surface (VHDL :5906) | audit | ce1d3be |
| V12-MEM-03 | Memory | b | ContentionModel.type_ not refreshed from saved Mmu.machine_type_ on load | audit | ce1d3be |
| V12-MEM-NIT-1 | Memory | (test) | V12-MEM-03's published test was non-discriminative (ZXN_ISSUE2 short-circuits is_contended_access); replaced with reviewer's stronger version | fix-of-reviewer | 64b9858 |
| V12-MEM-NIT-2 | Memory | (test) | V12-MEM-01-A leaked NR $50=0xE5 into subsequent tests; isolated via `nr_write(0x50, 0xFF)` | fix-of-reviewer | 64b9858 |
| V12-DIVMMC-01 | DivMMC | c | `SpiMaster::reset()` clobbered `rx_data_=0xFF` despite VHDL `i_reset='0'` hardwiring (zxnext.vhd:3285); spi_master.vhd:159-168 reset clause never fires | audit | (audit) |
| V12-DIVMMC-02 | DivMMC | b | CMD24 past-EOF returned 0x05 (data accepted) but should return 0x0D (write error) per SD spec § 7.3.3.3 | audit | (audit) |
| V12-DIVMMC-03 | DivMMC | c | R7 byte 0 missing cmd-version nibble (was 0x00, should be 0x10 per SD spec § 7.3.2.6) | reviewer-promoted | ce6a6ab |
| V12-DIVMMC-04 | DivMMC | c | CMD17/18 past-EOF R1 missing PARAMETER_ERROR bit 6 per SD spec § 7.3.2.1 | reviewer-promoted | ce6a6ab |
| V12-DIVMMC-06 | DivMMC | c | Pre-data-token byte handling missing — added `data_token_received_` flag | reviewer-promoted | ce6a6ab |
| V12-DIVMMC-01-NIT | DivMMC | c | `SpiMaster::rx_data_` member-init `0xFF` → `0x00` per VHDL `spi_master.vhd:74` `(others => '0')`; corrects 4 pre-existing tests that enshrined the wrong claim | fix-of-reviewer | 93af708 |
| V12-NMP-01 | NMI-MF-Port | a | NR $C4 b0 write updated `port_ff_reg(6)` correctly but did NOT mirror fan-out into `ula_int_disabled_` shadow nor call `video_timing_.set_interrupt_enable()`; NR $22 b2 already had it; corrected ctc_interrupts_test NR-C4-03 expectation 0x83 → 0x82 (VHDL-canonical) | audit | df0e4c2 |
| V12-NMP-02 | NMI-MF-Port | (NIT-promoted) | Port-0xFF write was the third writer to `port_ff_reg(6)` per VHDL :3614-3616 lacking the same fan-out as V12-NMP-01; multi-writer family closed | fix-of-reviewer | fdd21ca |
| V12-CPU-NIT-01 | CPU | c | Stale comment at z80_cpu.cpp:594-596 listed LDPIRX as non-F-writing; Pass-10 made LDPIRX F-writing per VHDL t80n.vhd:1277-1289 | reviewer-promoted | 2cc8453 |
| V12-CPU-NIT-02 | CPU | c | OUTINB extended-M1 emitted as raw `tstates += 1`; replaced with `contend_read_no_mreq(IR, 1)` BEFORE operand read per FUSE OUTI pattern + VHDL t80n_mcode.vhd:2516-2530 | reviewer-promoted | a57282c |

### Class-(d) confirmed architectural (DivMMC, all NEW catalogued)

- **V12-DIVMMC-05**: SPI 16-cycle FSM cycle-precise rewrite — multi-subsystem
- **V12-DIVMMC-07**: `divmmc_automap_*_q` falling-edge sub-cycle pipeline registers
- **V12-DIVMMC-08**: VHDL-impossible same-cycle Z80 OUT-OUT — spec doesn't expose this case

### Reviewer outcomes

| Subsystem | Audit verdict | Reviewer verdict | Fix-reviewer verdict |
|-----------|---------------|------------------|----------------------|
| Memory | 3 fixes | APPROVE-WITH-NITS (2 NITs) | APPROVE |
| DivMMC | 2 fixes | APPROVE-WITH-NITS (3 promoted+fixed by reviewer + 1 NIT) | APPROVE |
| NMI-MF-Port | 1 fix | APPROVE-WITH-NITS (1 missed → V12-NMP-02) | APPROVE |
| CPU | ZERO findings | APPROVE-WITH-NITS (2 reviewer-promoted NITs) | APPROVE |

### Convergence assessment (post-Pass-12)

**No subsystem qualified for converged-skip yet.** CPU was closest (audit ZERO) but reviewer found 2 NITs that count as findings under the convergence rule. Pass-13 will still need all 4 subsystems, but with the cumulative pattern of declining findings (Pass-11: 8, Pass-12: 12 — though 5 of those came from a single reviewer's ultrathink scrutiny on DivMMC), and CPU already at audit-zero, the trajectory points toward CPU first to converge.

### Subsystem-skip rule (introduced 2026-05-10 mid-Pass-12)

Once a subsystem returns audit=0 findings AND its independent reviewer returns APPROVE with no missed findings, it is converged and SKIPPED in subsequent passes. Memory/feedback rule documented at `feedback_task2_converged_subsystem_skip.md`.

## Pass-11 details

Pass-11 introduced a **unified fix+test+commit mandate** per finding (each bug must commit
its fix and a discriminative regression test together; reviewer must independently revert
the fix and confirm the test FAILs on the buggy code, then restore and confirm PASS).

### Findings (8 total, all fixed and merged)

| ID | Subsystem | Class | Summary | VHDL ref | Commit |
|----|-----------|-------|---------|----------|--------|
| V11-MEM-01 | Memory | c | `Mmu::rebuild_ptr` left `slots_[]` carrying verbatim NR-write value (e.g. 0xE5) after NR $50/$51 high-page write; reads via direct `read_ptr_` were correct, but save_state + load_state took the inconsistent path | zxnext.vhd:3052, :4686-4699, :6075-6082 | a8f8ecd |
| V11-DIVMMC-01 | DivMMC + SPI | b | `SpiMaster::reset()` did not pulse `deselect()` on selected slaves before clearing `cs_=0xFF`; CS rising edge resets each slave's protocol state on real hardware | zxnext.vhd:3308-3309 | d340bdd |
| V11-DIVMMC-02 | DivMMC | b | `is_nmi_hold()` returned registered `automap_held_` instead of combinational `automap` term, dropping same-cycle `instant_match` | divmmc.vhd:147-150 | d340bdd |
| V11-NMP-01 | NextREG | c | NR $81 read mask `0x7B` preserved bits 1:0 from user-written byte, but VHDL hardwires `nr_81_expbus_speed <= "00"`; bits should always read 0 | zxnext.vhd:5496, :6126 | 2aa35ac (audit) |
| V11-NMP-02 | NextREG | c | NR $0A write_handler stored raw `v` byte regardless of `nr_03_config_mode`; bits 7:5 leaked into cache when written outside config mode and surfaced via reset-fan-out | zxnext.vhd:5191-5198 | 2aa35ac (audit) |
| V11-NMP-03 | NextREG | c (NIT) | Reviewer-found: NR $06 had same cache-leak pattern (bit 2 `nr_06_ps2_mode`); fix mirrors V11-NMP-02 with mask 0xFB | zxnext.vhd:5161-5170 | 816dce9 (fix-of-reviewer) |
| V11-CPU-01 | IM2 | c | IM2 RETI decoder treated `DD ED 4D` as RETI; VHDL says S_DDFD_T4 → S_0 on any non-DDFD opcode; spurious reti_seen cleared legitimate ISR daisy-chain devices | im2_control.vhd:199-206 | d31e753 |
| V11-CPU-02 | Z80N PIXELDN | c | PIXELDN corrupted H[7:5] when band counter wrapped from 11; VHDL does composite `(b & R & C) + 1` truncated at bit 7; pre-fix's 4-step carry chain propagated band[1] carry into H[5] | t80n.vhd:900-921 | d31e753 |

### Cross-cutting

- **Release-build CMake fix** (commit `998db5a`): `jnext_memory` PUBLIC-links `jnext_peripheral` and `jnext_debug`. `mmu.cpp` dispatches the DivMMC overlay (DivMmc::read/write) and `mmu.h` inline read/write paths reference BreakpointSet::has_watchpoint. Without these PUBLIC link deps, Release builds (and the production end-user binary) failed to link the small CPU-only test executables (`fuse_z80_test`, `cpu_int_pulse_test`, `z80n_test`) and `jnext` itself with "undefined reference" errors. Debug builds happened to work via different inlining behaviour, masking the gap. Independently verified by reproducing the failure in another worktree.

### Reviewer outcomes

| Subsystem | Verdict | Notes |
|-----------|---------|-------|
| Memory | APPROVE | V11-MEM-01-A discriminative; no missed findings |
| DivMMC | APPROVE | SS-15 + NM-10 discriminative; 6 class-c observations recorded for context (none promotion-worthy) |
| NMI-MF-Port | APPROVE-WITH-NITS | 6 disc tests verified; 1 missed NIT → V11-NMP-03 cycle |
| NMI-MF-Port (fix-reviewer) | APPROVE | V11-NMP-03 disc verified; sibling audit of NRs 0x03, 0x10, 0x11 found cache-leak family closed |
| CPU | APPROVE | V11-CPU-01 + V11-CPU-02 disc; full IM2 FSM walked, 7 PIXELDN boundary inputs hand-checked vs VHDL |

### Convergence assessment

Pass-11 found 8 NEW class-(a/b/c) bugs across 4 subsystems plus 1 cross-cutting build bug.
**NOT converged.** Pass-12 required to test convergence claim. Encouraging signal:
- Pass-11 finding count (8) is the lowest of any pass except Pass-6 (4 — but that was an
  intentionally-narrow methodology pass).
- ZERO class-(a) findings (the most severe bucket) for the first time across an entire pass.
- ZERO class-(d) escalations introduced.
- The cache-leak-pattern family is now demonstrably closed (sibling audit of NRs 0x03,
  0x06, 0x0A, 0x10, 0x11 in fix-reviewer).

Sub-branches preserved across all 4 passes:
- P1: `task2/{memory,divmmc-sd-spi,nmi-mf-port,cpu-z80n-im2}-{review,reviewer}` + `task2/cpu-int-pulse-fix`
- P2: `task2/verify-{memory,divmmc-sd-spi,nmi-mf-port,cpu-z80n-im2}`
- P3: `task2/verify3-{memory,divmmc-sd-spi,nmi-mf-port,cpu-z80n-im2}`
- P4: `task2/verify4-{memory,divmmc-sd-spi,nmi-mf-port,cpu-z80n-im2}`

Worktrees under `.claude/worktrees/task2-*`, `task2-verify-*`, `task2-verify3-*`, `task2-verify4-*` — kept until integration branch lands on main.
