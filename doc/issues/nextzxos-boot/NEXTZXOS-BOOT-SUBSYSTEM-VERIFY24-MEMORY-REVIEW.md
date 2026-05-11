# Pass-24 Memory Subsystem — Convergence Pressure-Test Review

**Branch:** `task2/verify24-memory-reviewer` (off audit HEAD `05f157b7`)
**Reviewer:** Pass-24 independent blind reviewer
**Date:** 2026-05-11
**Scope:** Independent review of the Pass-24 Memory convergence
            pressure-test audit. Audit claimed 0 class-(a/b/c) findings
            with 1 new class-(d) escalation (V24-MEM-01: contention
            `mem_contend` decode uses MachineType (typ_sel-driven) but
            VHDL keys on `machine_timing_*` (tim_sel-driven)). Memory
            was last audited at Pass-14 and skipped P15..P23, so this
            is the first re-audit under the stricter Pass-19+
            enumeration-table mandate.

## Verdict: APPROVE-WITH-NITS

Memory convergence re-confirmed at Pass-24 under the stricter
Pass-19+ enumeration-table rigor. The audit's class-(a/b/c) zero-count
holds; no missed findings detected on independent re-walk of the
40+ Mmu APIs, 28 save/load fields, full ContentionModel surface, all
VHDL anchors at :4489-4493, :3662-3734, :4607-4699, :3036-3066,
:3914-3925, :2981-3008, :4623-4684.

The one NIT below concerns the **classification** of V24-MEM-01, not
its existence — the divergence is real and accurately characterised.
Reviewer believes V24-MEM-01 fits class-(c) (localised fan-out fix)
better than class-(d) (architectural refactor), based on prior
precedent in adjacent subsystems. Both classifications give the same
practical outcome (deferred pending user authorization), so this is
graded as a non-blocking NIT.

## Step 1 — Row-count verification

Audit claims approximately 120 rows in the enumeration table. Reviewer
counted by category:

| Section | Rows |
|---|---|
| Mmu — set/map function surface | 64 |
| Mmu — save/load schema fields | 27 |
| Ram — bank API | 8 |
| Rom — bank API | 7 |
| Contention — model surface | 17 |
| NR slot configuration write sites | 19 |
| Port write sites | 6 |
| Contention paths (M1/operand/IO) | 9 |
| `apply_*_paging_*_` helpers | 7 |
| RST/RETN trampoline interactions | 4 |
| **Total** | **168** |

168 rows, materially more than the "~120" claimed. The audit
under-counted but went broader than the headline number suggested,
which is the safer error direction. No row appears spurious or
duplicated.

## Step 2 — Spot-check of 8 ✓ rows

All 8 spot-checks pass with no surface divergence from VHDL.

### Spot-check #1 — `Mmu::write_nr_8e` (VHDL :3662-3670 / :3696-3704 / :3726-3734)

`Mmu::write_nr_8e` (mmu.cpp:695-754) atomically updates port_7ffd_reg,
port_dffd_reg, port_dffd_reg_6, port_1ffd_reg per the three FF update
processes in VHDL:
- bit3=1 → 7ffd(2:0) ← v(6:4) (VHDL :3664-3666)
- bit2=0 → 7ffd(4) ← v(0) (VHDL :3668-3670)
- bit3=1 → dffd(3) ← 0, dffd(2:0) ← "00" & v(7) (VHDL :3698-3704, profi='0' per :3797)
- 1ffd(2:0) atomic update (VHDL :3732-3734)
- bit3=0 → port_memory_ram_change_dly=1 suppresses MMU6/7 rebuild
  (VHDL :3814 / :4677 gate) — handled via half-rebuild path
✓ VHDL-faithful.

### Spot-check #2 — `Mmu::l2_port_readback()` (VHDL :3933)

VHDL composition: `port_123b_dat = seg(7:6) & "00" & shadow(3) &
rd_en(2) & en(1) & wr_en(0)`. C++ formula at mmu.h:1062-1070 emits
`seg<<6 | shadow<<3 | rd_en<<2 | en<<1 | wr_en`, leaving bits 4-5 as
0. Bit pattern matches exactly. ✓

### Spot-check #3 — `Mmu::l2_overlay_active_for(addr)` (VHDL :3036-3066)

VHDL low-half (cpu_a(15:14)="00"):
- MF case: sram_pre_override="000" (no L2 overlay).
- mmu_A21_A13(8)=0: sram_pre_override="110" (L2 bit set).
- nr_03_config_mode=1: sram_pre_override="110" (L2 bit set).
- else (legacy ROM): sram_pre_override="111" (L2 bit set, plus romcs).

VHDL high-half (cpu_a(15:14) != "00", :3060-3065):
- sram_pre_override(1) = `((!a15) OR (!a14)) AND seg(1) AND seg(0)`.
- For 0xC000-0xFFFF (a15=1, a14=1): both !a15 and !a14 are 0 → bit
  cleared, L2 always off.
- For 0x4000-0xBFFF with seg="11": bit set.

C++ at mmu.h:1124-1135: low-half always true in non-MF, high-half
gated on (`addr < 0xC000 AND seg == 0x03`). ✓ Matches.

### Spot-check #4 — `Mmu::apply_plus3_special_paging_()` (VHDL :4625-4632)

VHDL bit construction: `MMU<i> = "0000" & (b|a) & extra_bits`. Hand-
decoded for all 4 configs (B:A = 00, 01, 10, 11):
- (B=0,A=0): pages {0,1,2,3,4,5,6,7} → banks {0,1,2,3}
- (B=0,A=1): pages {8,9,A,B,C,D,E,F} → banks {4,5,6,7}
- (B=1,A=0): pages {8,9,A,B,C,D,6,7} → banks {4,5,6,3}
- (B=1,A=1): pages {8,9,E,F,C,D,6,7} → banks {4,7,6,3}

C++ table at mmu.cpp:466-471 emits exactly `{{0,1,2,3}, {4,5,6,7},
{4,5,6,3}, {4,7,6,3}}`. ✓

### Spot-check #5 — `Mmu::apply_legacy_rom_slots_()` (VHDL :4619-4646)

VHDL gate for EFF7(3)=1 OR (profi='1' AND dffd(4)='1') → MMU0/1 set
to 0x00/0x01 (RAM-at-0x0000). Since VHDL :3797 hardwires profi='0',
the profi term is dead code. C++ at mmu.cpp:410-437 correctly checks
only `port_eff7_reg_3_` (EFF7(3)) and falls through to sram_rom-
derived ROM map otherwise. The 0xFF sentinel write at line 435-436
matches VHDL :4643-4644 default. ✓

### Spot-check #6 — `Mmu::unlock_paging()` (V3-MEM-class-(a) fix, VHDL :3654-3656)

VHDL :3654 sets `port_7ffd_reg(5) <= '0'` on NR 0x08 write bit 7=1.
C++ at mmu.h:639-642 clears both `paging_locked_` AND `port_7ffd_ &
~0x20` — capturing both the standalone lock-mirror AND the raw bit-5
in the port register. ✓ V3-MEM fix preserved.

### Spot-check #7 — `Mmu::compose_bank_()` (VHDL :3763-3766)

VHDL composition:
- bank(2:0) = port_7ffd_reg(2:0)
- bank(4:3) = port_7ffd_reg(7:6) when pentagon else port_dffd_reg(1:0)
- bank(5) = port_dffd_reg(2) when !pentagon else (p1024_en AND p7ffd(5))
- bank(6) = '0' when pentagon or profi else port_dffd_reg(3)

C++ at mmu.cpp:373-389 implements all four bits with correct Pentagon
branching, including the N8F-05 bit(6)='0' forced rule in Pentagon
mode (line 382 comment). Pentagon-1024 bit(5) gating verified. ✓

### Spot-check #8 — `Mmu::set_machine_type` (V5/V7/V12-MEM cumulative fixes)

mmu.h:803-848 set_machine_type carries forward all four prior fixes:
- V5-MEM: skip rebuild during +3 special paging (line 823-828).
- V7-MEM: only refresh slots already in legacy ROM mode (line
  846-847).
- V12-MEM-01: pass `set_nr_sentinel=false` so verbatim 0xE0..0xFE
  values survive (line 846-847).
- General comment block (lines 808-845) documents the VHDL :2981-3008
  combinational sram_rom routing and the no-port_memory_change_dly
  pulse on machine_type changes (:3813). ✓

All 8 spot-checks pass. No surface divergence vs VHDL.

## Step 3 — V24-MEM-01 class-(d) escalation verification

### V24-MEM-01 existence — confirmed

VHDL `mem_contend` at zxnext.vhd:4489-4493 keys on `machine_timing_48
/ machine_timing_128 / machine_timing_p3` (lines 5761-5777 derive
these from `eff_nr_03_machine_timing`, which is the latched/effective
shadow of `nr_03_machine_timing`).

VHDL `machine_type_48 / machine_type_128 / machine_type_p3` (lines
5741-5757) derive from `nr_03_machine_type` (NR 0x03 bits[2:0]) —
a DIFFERENT field. NR 0x03 bits[6:4] write tim_sel (lines 5125-5132),
bits[2:0] write typ_sel (lines 5138-5144, only when config_mode=1).
A single NR 0x03 write CAN set them differently.

C++ contention.cpp:
- `is_contended_access()` (line 112) keys on `type_` (MachineType
  enum = typ_sel-driven via `rebuild_for_type` / NR 0x03 commit).
- `contention_tick(...)` (line 234) keys on same `type_`.
- `port_contend(...)` (line 168) keys on same `type_` for
  port_7ffd_active gate.

C++ mmu.h:
- `mem_contend_for_(addr)` (line 1193) keys on `machine_type_`
  (typ_sel-driven).

VHDL keys these on `machine_timing_*` (tim_sel-driven). When firmware
writes NR 0x03 with `tim_sel != typ_sel`, the four C++ decode sites
above lag the VHDL by one axis.

**V24-MEM-01 is a genuine semantic divergence vs VHDL.** ✓

### V24-MEM-01 boot impact — confirmed nil

NR 0x03 commit handler at emulator.cpp:2247+ fans out both tim_sel
(line 2270 `set_nr_03_machine_timing`) and typ_sel (line ~2330+
`set_machine_type`) per VHDL :5124-5145. Real TBBlue firmware NR 0x03
writes always pair matching tim/typ pairs (e.g., NR 0x03 = $B3 = bits
6:4=011 + bits 2:0=011 = both +3). The divergence only fires on
custom firmware that intentionally sets mismatched bits — not
observed in NextZXOS boot trace.

Audit's "real-boot impact: nil" claim ✓.

### V24-MEM-01 classification — class-c, not class-d (NIT)

Audit classifies V24-MEM-01 as class-(d) requiring "structural
separation: add `machine_timing_` field to ContentionModel + Mmu,
fan-out from NR 0x03 timing-commit path, persist in save/load
schemas".

**Reviewer disagrees.** Compare to adjacent precedent:

- **V13-MEM-01** (Pass-13) — NR 0x69 bit 7 → mmu_.set_l2_enable
  cross-subsystem mirror gap. Class-(a). Fix added: 1-line Emulator
  fan-out + 1 setter + tests. No save/load schema change required
  (l2_enable_ already persisted).
- **V19-IM2-04** (Pass-19) — int_line_asserted polling drives CPU
  /INT. Class-(b). Fix added: polling path between Im2Controller
  and Z80Cpu. Multi-line refactor + tests, no architectural change
  to either subsystem.
- **V20-IM2-01** (Pass-20) — pulse-mode CPU /INT polling gap.
  Class-(b). Same fan-out pattern as V19-IM2-04.
- **CPU `set_machine_timing_48_or_p3`** (Im2Controller / Z80Cpu) —
  the analog of V24-MEM-01 already exists for IM2 and CPU
  subsystems, wired through NR 0x03 commit at emulator.cpp:2308-2313.

V24-MEM-01's fix path is **identical in shape** to those precedents:
add `machine_timing_` storage (1 byte or 1 bool depending on
granularity) to Mmu + ContentionModel, fan out from NR 0x03 commit
(2-3 lines next to existing `im2_.set_machine_timing_48_or_p3`),
update the 4 decode sites (`mem_contend_for_`, `is_contended_access`,
`contention_tick`, `port_contend`), bump save_state schemas by 1-2
bytes.

The existing class-(d) memory items (aggregate report
NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS.md:49-50) are genuinely
architectural — they require a CPU half-cycle model. V24-MEM-01 does
NOT require any such infrastructure; it is a localised fan-out
correction analogous to multiple prior class-(b) fixes.

**Reviewer reclassifies V24-MEM-01 to class-(c).** This is a NIT, not
a blocking finding: the practical outcome is the same (deferred
pending user authorization), and the boot-impact-nil claim is
unchanged. Update aggregate report accordingly (move from class-(d)
column to class-(c)+follow-up column).

**Important caveat**: there is one architectural sub-aspect of this
family that genuinely IS class-(d), and the audit could legitimately
have called it out: the use of boot-time `config_.type` at
emulator.cpp:3492 (port 0x0FFD `port_p3_float` decode) bypasses BOTH
mmu_'s runtime `machine_type_` AND a hypothetical `machine_timing_`
shadow. Fixing that consistently requires a project-wide convention
on whether port handlers consult boot type or runtime type — a
broader decision than just V24-MEM-01. But that aspect is not
described in the audit's V24-MEM-01 statement and is a separate item.

## Step 4 — Prior-fix re-verification spot-check

Re-walked five prior memory-class fixes; all preserved correctly.

| Prior fix | Pass | Verification |
|---|---|---|
| V3-MEM (unlock_paging clears both lock + port_7ffd bit 5) | P3 | mmu.h:639-642 ✓ |
| V4-MEM (verbatim nr_mmu_[] persistence) | P4 | save mmu.cpp:880, load mmu.cpp:937 ✓ |
| V8-MEM (128K shares ZXN sram_rom branch) | P8 | mmu.h:876-897 case fall-through ✓ |
| V9-MEM (port_7ffd_active gate in port_contend) | P9 | contention.cpp:167-175 ✓ |
| V12-MEM-02 (port_1ffd_special_old_ persistence) | P12 | mmu.cpp:865-869 save, mmu.cpp:928-929 load ✓ |
| V13-MEM-01 (NR 0x69 bit 7 → l2_enable + bit 6 → 7ffd(3)) | P13 | emulator.cpp:2547-2555 ✓ |
| G46(b) Wave 8 (engage_legacy_*_paging wrappers) | wave 8 | mmu.h:954-955 + emulator.cpp:1800 dispatch ✓ |

All seven re-verified; no fix has regressed since its original commit.

## Step 5 — Cross-cutting families final sweep

### Multi-writer FF mirrors

`port_7ffd_reg` (4 VHDL writers per :3650/:3654/:3658/:3662): all 4
have C++ counterparts:
- :3652 port_7ffd_wr → `Mmu::map_128k_bank` / `apply_legacy_paging_`
- :3656 nr_08_we bit 7 → `Mmu::unlock_paging`
- :3660 nr_69_we → `Mmu::set_port_7ffd_bit3` (V13-MEM-01)
- :3662-3670 nr_8e_we → `Mmu::write_nr_8e`

Similar walkthrough for `port_1ffd_reg`, `port_dffd_reg`,
`port_dffd_reg_6`, `port_eff7_reg_*`, `MMU0..7` — all writers
accounted for. ✓

### NR readback compositions

- NR 0x50..0x57 (mmu page surface): `get_page(i)` returns `nr_mmu_[i]`
  verbatim including 0xFF sentinel (V4-MEM). ✓
- NR 0x69 read at emulator.cpp: composes `l2_enable_<<7 | shadow<<6 |
  ...`. ✓
- NR 0x8C: `nr_8c_reg_` raw 8-bit (:2257 / :6156). ✓
- NR 0x8E read at mmu.cpp:770-788: bit-decomposition matches VHDL
  :6159 expression byte-by-byte. ✓
- NR 0x8F read at emulator.cpp: `(00 00 00 0) & nr_8f_mode_` matches
  VHDL :6162. ✓

### Save/load symmetry

Every save_state field has a matching load_state read in identical
order (mmu.cpp:798-870 save → mmu.cpp:874-940 load). 28 fields
verified pair-by-pair.

### Reset semantics (hard/soft fold)

`Mmu::reset(bool hard)` (mmu.cpp:52-166) folds both hard and soft per
VHDL top-level `reset='1'` semantics. `nr_8c_reg_` lo→hi nibble copy
(:2253-2256) handled. `boot_rom_en_` re-arm if `config_mode_` true
(:5109-5111). ✓

### Cycle-accurate FSM (port_memory_change_dly)

VHDL per-clock arbitration vs C++ per-port-write granularity:
back-to-back FPGA-clock triggers can't occur at the CPU OUT level
(slowest Z80 cycle ≥ 4 clocks). The C++ per-write
`apply_paging_update_` is functionally equivalent. ✓ (Existing
class-(d) architectural carry-over — half-cycle model required for
strict fidelity. Same as P14.)

## Baseline test status

- Release build: clean
- ctest: **38/38 PASS** (including mmu_tests / contention_tests)
- FUSE Z80 opcode suite: **1356/1356 PASS**
- Regression suite: **33/0/0 PASS**

All baselines green; no regressions in this pass.

## Convergence-stability statement

The memory subsystem is **convergence-stable at Pass-24** under the
stricter Pass-19+ enumeration-table rigor. The single new finding
(V24-MEM-01) is in the same architectural-tier-or-near-tier bucket
that exists in the class-(d) pool already (the IM2/CPU
machine_timing_48_or_p3 fan-out is its precedent), and does not
require revisiting any prior P3..P14 fix.

Memory convergence holds for the second consecutive verification
(P14 first, P24 second under stricter rigor). Reviewer recommends
maintaining the convergence-skip status for Memory through Pass-25+.

## Verdict summary

- **Audit's class-(a/b/c) zero count**: CONFIRMED. No missed
  findings.
- **Audit's enumeration table**: comprehensive (168 rows vs ~120
  claimed) and well-cited.
- **V24-MEM-01 existence and boot-impact-nil**: CONFIRMED.
- **V24-MEM-01 class-(d) classification**: DISPUTED — reviewer
  argues class-(c) fits better given V13-MEM-01 / V19-IM2-04 /
  V20-IM2-01 precedents. Non-blocking NIT; same deferred outcome.
- **Prior memory fixes (V3..V13 + G46(b) Wave 8)**: ALL HOLD.
- **Cross-cutting families**: clean (FF mirrors, NR readbacks,
  save/load, reset, cycle-accurate FSM).
- **Test invariants**: ctest 38/38, FUSE 1356/1356, regression
  33/0/0 — all green.

**APPROVE-WITH-NITS.** The single NIT is the class-(c) vs class-(d)
classification of V24-MEM-01; the divergence itself is correctly
identified and characterised.

## Files reviewed

- doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY24-MEMORY.md
  (446 lines, audit deliverable)
- src/memory/mmu.h (1426 lines)
- src/memory/mmu.cpp (987 lines)
- src/memory/ram.{h,cpp}, src/memory/rom.{h,cpp}
- src/memory/contention.h (265 lines), src/memory/contention.cpp (304 lines)
- src/core/emulator.cpp (NR 0x03 / NR 0x69 / port handler sites, ~50 sites)
- test/contention/contention_test.cpp (test setup patterns)
- VHDL oracle: cores/zxnext/src/zxnext.vhd lines 1099, 1103, 1282-1285,
  1377, 2457-2458, 2589-2594, 2604, 2937-3132, 3036-3066, 3199-3204,
  3640-3742, 3768-3801, 3914-3935, 4481-4513, 4607-4699, 5121-5151,
  5741-5777, 6075-6082, 6155-6162
