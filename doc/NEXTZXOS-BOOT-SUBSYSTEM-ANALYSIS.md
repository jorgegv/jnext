# NextZXOS Boot Critical Subsystem Analysis — Aggregated Report

**Branch:** `nextzxos-boot-subsystem-analysis` (off `main`)
**Date:** 2026-05-09
**Audit scope:** memory, divmmc + sd_card + spi, nmi + multiface + port + nextreg, cpu (Z80 + Z80N + IM2)
**Methodology:** parallel analysis agents (one per subsystem, ultrathink mode, VHDL-as-oracle), each followed by an independent reviewer agent on a separate branch (per CLAUDE.md "code review must never be done by the same agent that wrote the code"). Reports for each subsystem live alongside this file (one analysis report + one review report per subsystem).

## Executive summary

Eleven VHDL-fidelity discrepancies were found across the four boot-critical subsystems. **All eleven were classified as clear bugs (class a) and were fixed VHDL-faithfully**; none required reverts. One additional follow-up fix was applied based on a reviewer's finding (INT pulse window machine-awareness). Every fix carries a VHDL line citation in its commit message and code comment.

| Subsystem | Fixes | Reviewer verdict | G46(b) relevance |
|-----------|-------|------------------|------------------|
| Memory (mmu/ram/rom/contention) | 2 | APPROVE-WITH-NITS | orthogonal |
| DivMMC + SD + SPI | 3 | APPROVE-WITH-NITS | orthogonal (Bug B inert in trace) |
| NMI + Multiface + Port + NextREG | 5 | APPROVE-WITH-NITS | orthogonal |
| CPU (Z80 + Z80N + IM2) | 1 systematic + 1 follow-up | APPROVE-WITH-NITS | **PUSH imm byte order CONFIRMED CORRECT** (rules out top G46(b) hypothesis); INT pulse-window machine-awareness fix is the only finding with potential G46(b) relevance |

**Test status (post-merge, on integration branch):**
- `ctest`: 37/37 PASS (added new `cpu_int_pulse_tests` from the INT pulse fix)
- FUSE Z80 opcode suite: **1356/1356 PASS**
- Full regression suite (`test/00regression/regression.sh`): **33/0/0**

**Subsystem completeness assessment.** The original Task 2 list (memory, divmmc, nmi, port) was extended to include three additional boot-critical surfaces:

- **CPU (z80_cpu + z80n_ext + im2)** — Z80N opcodes are central to the supervisor (e.g. `NEXTREG $8E,$03` is the bank-3 entry instruction `ED 91 8E 03`); RST/RETN/EX (SP),HL semantics underpin every bank-flip wrapper; IM2 vsync ISR cadence drives every supervisor `EI;HALT;RET` cycle.
- **SD-card pipeline (sd_card + spi)** — DivMMC AUTOMAP plus SD/SPI byte-pump are inseparable for boot. tbblue.fw reads FAT32 via this stack.
- **Multiface (peripheral)** — Wave 1 added; its ROM (loaded from SD) handles NMI + bank flip.

The expanded list (`memory + divmmc/sd/spi + nmi/multiface/port + cpu/z80n/im2`) covers every subsystem the Z80 CPU touches between cold reset and the BASIC welcome screen. No further additions were proposed by any reviewer.

## Findings by subsystem

### Memory (mmu/ram/rom/contention)

Branch: `task2/memory-review` (analysis), `task2/memory-reviewer` (review)
Reports: `doc/NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS-MEMORY.md`, `…-MEMORY-REVIEW.md`

**Fixes:**

1. **NR $5x,$FF per-slot semantics** (`f832f38`) — VHDL `zxnext.vhd:4607-4699` stores `$FF` verbatim in MMU<i>; arbiter at `:3037-3066` then handles each slot per its own logic. Old jnext fallback (`engage_legacy_ram_paging` for slots 6/7, `map_rom(i,0)` for slots 2-5) silently forced non-$FF mappings, diverging from VHDL. Fix introduces per-slot helper `engage_legacy_rom_paging_slot(i)` (preserves the OTHER ROM slot's prior NR mapping) and uses `set_page(i, 0xFF)` for slots 2-7 (resolves to inactive `sram_pre_active='0'` per VHDL `:3061`).

2. **+3 special-paging arbitration** (`45d8b30`) — VHDL `zxnext.vhd:4623-4684` describes a unified arbiter handling entry/exit/legacy branches. jnext had three independent code paths racing each other on +3 1FFD writes. New `apply_paging_update_()` consolidates them. `port_1ffd_special_old_` model differs from VHDL's per-cycle decay but is functionally equivalent (verified by trace simulation across all 4 special-paging configurations).

**Reviewer note:** A1 reviewer found the merge with the NMI agent's branch would conflict on `src/core/emulator.cpp:1372-1403`. Resolution applied: take memory agent's version (more VHDL-faithful — per-slot helper preserves other slot's mapping; slot 6/7 with `$FF` correctly inactivates per `:3061` rather than forcing `port_7ffd_bank` composition).

**Cross-check vs G46(b):** Both fixes are inert in the current observed boot trace (no NR $52..$57 with $FF; no +3 special paging exercised on `--machine next`). They are latent correctness improvements, not the immediate G46(b) trigger.

### DivMMC + SD-card + SPI

Branch: `task2/divmmc-sd-spi-review` (analysis), `task2/divmmc-sd-spi-reviewer` (review)
Reports: `doc/NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS-DIVMMC-SD-SPI.md`, `…-DIVMMC-SD-SPI-REVIEW.md`

**Fixes (all in commit `399c9ae`):**

1. **Bug A — NR $BB bit 0 (`automap_nmi_delayed_on` at PC=$0066) was not decoded.** VHDL `zxnext.vhd:2908`. Silent under default $BB=$CD (bit 1 also fires) but a real divergence that surfaces on $BB=$01 configurations.
2. **Bug B — NR $BB bit 7 ($3DXX ROM3 instant_on wildcard) completely missing.** VHDL `zxnext.vhd:2898-2899`. Enabled by default; old tests had "passes vacuously" comments because the path was never reachable. Bug B is the only DivMMC finding with theoretical G46(b) relevance — but EOD-24 trace shows no $3DXX PCs in the active boot trail, so confirmed inert.
3. **Bug C — SPI ports $E7 / $EB had no `port_spi_io_en` gate.** VHDL `zxnext.vhd:2419, 2620-2621` (NR $83 bit 3).

**No fix needed (verified):** SPI byte pipeline VHDL-faithful; SD-card `persistent_response_byte_` + CSD/CID synthesis are documented intentional ZEsarUX-faithful compatibility hacks; automap pipeline collapse is functionally equivalent at byte granularity; port $E3 LSB-decode + mapram OR-latch + NR $09 bit 3 clear + G46(a) RETN delayed clear are correct.

**Reviewer added:** doc nit (NR $BB=$CD bit decomposition prose error in author's report; fix itself is correct), and 4 coverage observations (no unit test for the new SPI port gating; possible follow-up: extract `Emulator::is_spi_io_enabled()` helper).

### NMI + Multiface + Port + NextREG

Branch: `task2/nmi-mf-port-review` (analysis), `task2/nmi-mf-port-reviewer` (review)
Reports: `doc/NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS-NMI-MF-PORT.md`, `…-NMI-MF-PORT-REVIEW.md`

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
Reports: `doc/NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS-CPU.md`, `…-CPU-REVIEW.md`

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

## G46(b) cross-check (aggregate)

Per-subsystem reviewer assessments are unanimous: **none of the eleven Task-2 fixes is on the G46(b) slide-cascade critical chain.** The G46(b) bug is supervisor-stack divergence between RST $08 hits #2 and #3 (3 missing PUSHes / 3 extra POPs vs CSpect; per memory `project_g46b_2026_05_09_eod24_nr8e03.md`), rooted upstream at the supervisor's execution-path divergence — not in any subsystem's spec compliance.

The one potentially-relevant finding is **INT pulse window machine-awareness** (CPU follow-up fix `3c89104`): the supervisor's `EI;HALT;RET` pattern depends on IM2 vsync edges arriving with the correct cadence; a 4-cycle wrong pulse width could shift one or more boundary-case ISR firings. Whether this actually moves the supervisor's stack ascent has NOT been verified by re-running the G46(b) cycle on this branch — that's recommended as an immediate next step before the broader G46(b) investigation continues.

## Open questions / deferred work

1. **G46(b) cycle re-run on integration branch** — does the INT pulse fix change the supervisor's behaviour between RST $08 hits #2 and #3? If yes, that pinpoints a subtle CPU-timing contributor to G46(b). If no, G46(b) remains rooted at supervisor execution-path divergence (the leading hypothesis).
2. **R-1 and R-2 NMI follow-ups** — `nr_02_pending_*` reset scope (VHDL `:1730`) and `nr_da_iotrap_cause_` `nmi_accept_cause` gate (VHDL `:3871`). Not blocking; deferred to a future NMI-pipeline pass.
3. **Test-coverage gaps** — A1 reviewer flagged 1 new public API + 4 new private helpers + 1 new state field with no unit tests; A2 reviewer flagged no test for the new SPI port gating; A3 reviewer flagged 4 missing test rows (NMI-1 ExpBus selector regression, NMI-3 second-NMI-fires regression, NR-2 slot-2-5 $FF, NMI-2 already covered) plus 7 stale doc references in the NMI test plan and 4 stale source comments. Deferred.
4. **Doc nit** — A2 reviewer noted NR $BB=$CD bit decomposition prose error in the DivMMC author's report (the fix itself is correct). Cosmetic.
5. **`port_1ffd_special_old_` decay model** — A1 reviewer noted not bit-faithful to VHDL `:3736-3738` but functionally equivalent for all paging-trigger permutations. Acceptable approximation.
6. **`StateReader::read_u8()`** lacks bounds check — pre-existing latent issue not introduced by this work.

## Test status (final, integration branch)

```
ctest                              37/37 PASS
fuse_z80                       1356/1356 PASS
test/00regression/regression       33/0/0
```

## Branch state

```
Branch: nextzxos-boot-subsystem-analysis (off main)
HEAD:   fda54be merge(task2): CPU subsystem reviewer report
Behind: 0
Ahead:  17 commits (5 merges + 12 underlying)
Pushed: NO
```

Sub-branches (preserved, not yet deleted):
- `task2/memory-review`, `task2/memory-reviewer`
- `task2/divmmc-sd-spi-review`, `task2/divmmc-sd-spi-reviewer`
- `task2/nmi-mf-port-review`, `task2/nmi-mf-port-reviewer`
- `task2/cpu-z80n-im2-review`, `task2/cpu-z80n-im2-reviewer`
- `task2/cpu-int-pulse-fix`

Worktrees under `.claude/worktrees/task2-*` — can be removed once the integration branch is accepted and merged to `main`.
