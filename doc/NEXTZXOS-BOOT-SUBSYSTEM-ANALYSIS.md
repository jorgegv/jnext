# NextZXOS Boot Critical Subsystem Analysis — Aggregated Report

**Branch:** `nextzxos-boot-subsystem-analysis` (off `main`)
**Date:** 2026-05-09
**Audit scope:** memory, divmmc + sd_card + spi, nmi + multiface + port + nextreg, cpu (Z80 + Z80N + IM2)
**Methodology:** parallel analysis agents (one per subsystem, ultrathink mode, VHDL-as-oracle), each followed by an independent reviewer agent on a separate branch (per CLAUDE.md "code review must never be done by the same agent that wrote the code"). After the first pass closed, a **blind verification re-audit** was run with four fresh agents (also ultrathink, also one per subsystem) — explicitly forbidden from reading the first-pass reports — to verify the post-fix state and to actively hunt for what the first pass missed. Reports for each subsystem live alongside this file (one analysis report + one review report + one verification report per subsystem).

## Executive summary

**First pass:** eleven VHDL-fidelity discrepancies were found across the four boot-critical subsystems and fixed VHDL-faithfully. One additional follow-up was applied based on a reviewer's finding (INT pulse window machine-awareness).

**Verification pass (blind re-audit):** the verification agents were forbidden from reading the first-pass reports and were tasked with actively hunting for what the first pass missed. **They found six additional class-(a) bugs**, two of which are potentially G46(b)-relevant and which the first pass entirely overlooked. The verification pass was therefore highly worthwhile — it caught spec violations that even the first pass + independent reviewer pair missed.

| Subsystem | First-pass fixes | Verification fixes | First-pass verdict | Verification verdict | G46(b) relevance |
|-----------|------------------|---------------------|---------------------|----------------------|------------------|
| Memory | 2 | **2 (NEW)** | APPROVE-WITH-NITS | NEW FINDINGS | **NR $8C cache staleness — high candidate** (alt-ROM enable doesn't propagate until next paging port write) |
| DivMMC + SD + SPI | 3 | 0 (1 class-(b) reported) | APPROVE-WITH-NITS | 0 class-(a), 1 class-(b) | orthogonal |
| NMI + Multiface + Port + NextREG | 5 | **3 (NEW)** | APPROVE-WITH-NITS | NEW FINDINGS | orthogonal (functionally invisible / not on boot path) |
| CPU (Z80 + Z80N + IM2) | 1 systematic + 1 follow-up | **1 (NEW)** | APPROVE-WITH-NITS | NEW FINDINGS | **Z80N global tstates not incrementing — high candidate** (Z80N ran at zero global cycles → contention, INT pulse window, IM2 cadence all wrong) |

**Total fixes on integration branch: 17** (11 first-pass + 6 verification).

**Test status (post all-merge, on integration branch):**
- `ctest`: **37/37 PASS**
- FUSE Z80 opcode suite: **1356/1356 PASS**
- Full regression suite: **33/0/0**

**G46(b) crosscheck deferred** (will be run later by the user). The two verification-pass findings most likely to be G46(b)-relevant are flagged in the per-subsystem sections below.

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

## G46(b) cross-check (first pass)

First-pass per-subsystem reviewer assessments were unanimous: **none of the eleven first-pass fixes is on the G46(b) slide-cascade critical chain.** The G46(b) bug is supervisor-stack divergence between RST $08 hits #2 and #3 (3 missing PUSHes / 3 extra POPs vs CSpect; per memory `project_g46b_2026_05_09_eod24_nr8e03.md`).

The first-pass finding most-plausibly relevant was **INT pulse window machine-awareness** (CPU follow-up fix `3c89104`): the supervisor's `EI;HALT;RET` pattern depends on IM2 vsync edges arriving with the correct cadence; a 4-cycle wrong pulse width could shift boundary-case ISR firings.

The verification pass added two more high-leverage candidates (see below).

---

# Verification re-audit (second pass, blind)

After the first-pass merge closed, four fresh agents (one per subsystem) were launched off integration-branch HEAD `e6bd9ce`, all in ultrathink mode, **forbidden from reading any first-pass `doc/NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS-*.md` file**. Each was told to audit the post-fix state against VHDL from scratch and to actively hunt for what the first pass missed. Per-subsystem verification reports: `doc/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY-{MEMORY,DIVMMC-SD-SPI,NMI-MF-PORT,CPU}.md`.

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

## G46(b) cross-check (aggregate, both passes)

The two highest-leverage candidates from this audit are:

- **NR $8C lock-bit cache staleness (memory verification fix #1)** — bank 0 supervisor wrapper does `NEXTREG $8C, $80; RET`; pre-fix the next M1 fetch in slot 0/1 reads stale ROM mapping. Direct candidate for "wrong bank visible at $3F00" (EOD-23) and supervisor stack divergence between RST $08 hits #2 and #3.
- **Z80N global tstates not incrementing (CPU verification fix #6)** — Z80N at zero global cycle cost. Corrupts INT pulse window check and contention timing. Verification agent specifically notes: "3 spurious INT events would explain exactly 6 bytes of stack growth" — directly matches the EOD-22 Wave 7 finding.

Either or both could plausibly be G46(b) root cause. **G46(b) cycle crosscheck has been deferred** (will be run later by the user) on the post-merge integration branch — that's the verdict.

## Open questions / deferred work

1. **G46(b) cycle re-run** — does either of the two high-leverage verification fixes change the supervisor's behaviour between RST $08 hits #2 and #3? Deferred to user.
2. **R-2 NMI** — `nr_da_iotrap_cause_` lacking `nmi_accept_cause` gate (VHDL `:3870-3892`). Still deferred.
3. **Memory class-(b) #1**: NR $08 readback returns shadow not effective. 1-line fix possible.
4. **Memory class-(b) #2**: Layer 2 `port_123b` segment-mask asymmetry vs VHDL.
5. **DivMMC class-(b)**: `rom3_selected()` vs `sram_rom3()` mismatch (boot-irrelevant; affects bank-1 tape traps).
6. **NMI class-(b)**: NR 0x02 readback bit 7 not modelled.
7. **CPU class-(b) #1**: M1-fetch contention skipped on Z80N raw-read path.
8. **CPU class-(b) #2**: `Im2Controller::ack_vector()` early state advance.
9. **Test-coverage gaps** — multiple. See per-subsystem reports.
10. **`port_1ffd_special_old_` decay model** — functionally equivalent approximation.
11. **`StateReader::read_u8()`** lacks bounds check — pre-existing.

## Test status (final, integration branch, post both passes)

```
ctest                              37/37 PASS
fuse_z80                       1356/1356 PASS
test/00regression/regression       33/0/0
```

## Branch state

```
Branch: nextzxos-boot-subsystem-analysis (off main)
HEAD:   <after report commit>
Pushed: NO
```

Sub-branches (preserved, not yet deleted):

First pass:
- `task2/memory-review`, `task2/memory-reviewer`
- `task2/divmmc-sd-spi-review`, `task2/divmmc-sd-spi-reviewer`
- `task2/nmi-mf-port-review`, `task2/nmi-mf-port-reviewer`
- `task2/cpu-z80n-im2-review`, `task2/cpu-z80n-im2-reviewer`
- `task2/cpu-int-pulse-fix`

Verification pass:
- `task2/verify-memory`
- `task2/verify-divmmc-sd-spi`
- `task2/verify-nmi-mf-port`
- `task2/verify-cpu-z80n-im2`

Worktrees under `.claude/worktrees/task2-*` and `.claude/worktrees/task2-verify-*` — can be removed once the integration branch is accepted and merged to `main`.
