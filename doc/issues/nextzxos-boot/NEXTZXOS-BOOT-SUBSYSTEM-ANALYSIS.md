# NextZXOS Boot Critical Subsystem Analysis — Aggregated Report

**Branch:** `nextzxos-boot-subsystem-analysis` (off `main`)
**Date:** 2026-05-09
**Audit scope:** memory, divmmc + sd_card + spi, nmi + multiface + port + nextreg, cpu (Z80 + Z80N + IM2)
**Methodology:** parallel analysis agents (one per subsystem, ultrathink mode, VHDL-as-oracle). The audit is being repeated **iteratively** with a fresh set of four blind agents each pass — each pass blind to all prior reports — until reviews honestly converge to zero class-(a) bugs across all four subsystems. The user's standing instruction: "correctness is the priority, not premature convergence."

## Executive summary

| Pass | Method | Class-(a) bugs found | Notes |
|---|---|---|---|
| 1 | 4 analysis + 4 independent reviewers | 11 + 1 follow-up | All reviewers APPROVE-WITH-NITS |
| 2 | 4 fresh blind (forbidden from P1) | 6 | Two high-leverage G46(b) candidates |
| 3 | 4 fresh blind (forbidden from P1+2) | 11 | Refinements of P2 + class-(b) promotions |
| 4 | 4 fresh blind (forbidden from P1+2+3) — sharper methodology | 17 | NMI 80% sweep + CPU duals |
| 5 | 4 fresh blind (forbidden from P1+2+3+4) — final-convergence angles | **11** | NR $09/$15 cross-fix landed; LDPIRX confirmed-correct; Z80N M1 contention promoted |
| **Total (5 passes)** | | **57 class-(a) bugs** + 1 follow-up | **Audit NOT yet converged** |

**Pass-by-pass count by subsystem:**

| Subsystem | P1 | P2 | P3 | P4 | P5 | Trend |
|-----------|----|----|----|----|----|-------|
| Memory | 2 | 2 | 3 | 3 | 2 | descending |
| DivMMC + SD + SPI | 3 | 0 | 2 | 1 | 2 | bouncing low |
| NMI + Multiface + Port + NextREG | 5 | 3 | 3 | 9 | 6 | greenfield-driven |
| CPU (Z80 + Z80N + IM2) | 1+1 | 1 | 3 | 4 | 1 | **descending** |
| **Pass total** | **11+1** | **6** | **11** | **17** | **11** | |

**Total fixes on integration branch: 58** (12 P1 + 6 P2 + 11 P3 + 17 P4 + 11 P5).

**Test status (post all-merge, integration branch):**
- `ctest`: **37/37 PASS**
- FUSE Z80 opcode suite: **1356/1356 PASS**
- Full regression suite: **33/0/0**

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

## Test status (final, integration branch, post all 4 passes)

```
ctest                              37/37 PASS
fuse_z80                       1356/1356 PASS
z80n_test                          85/85 PASS
test/00regression/regression       33/0/0
```

## Branch state

```
Branch: nextzxos-boot-subsystem-analysis (off main)
Total fixes: 45 class-(a) bugs + 1 follow-up = 46
Pushed: NO
```

Sub-branches preserved across all 4 passes:
- P1: `task2/{memory,divmmc-sd-spi,nmi-mf-port,cpu-z80n-im2}-{review,reviewer}` + `task2/cpu-int-pulse-fix`
- P2: `task2/verify-{memory,divmmc-sd-spi,nmi-mf-port,cpu-z80n-im2}`
- P3: `task2/verify3-{memory,divmmc-sd-spi,nmi-mf-port,cpu-z80n-im2}`
- P4: `task2/verify4-{memory,divmmc-sd-spi,nmi-mf-port,cpu-z80n-im2}`

Worktrees under `.claude/worktrees/task2-*`, `task2-verify-*`, `task2-verify3-*`, `task2-verify4-*` — kept until integration branch lands on main.
