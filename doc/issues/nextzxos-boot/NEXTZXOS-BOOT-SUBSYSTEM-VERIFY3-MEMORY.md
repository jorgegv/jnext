# NextZXOS Boot Subsystem — Pass-3 Memory Verification Re-audit

**Branch:** `task2/verify3-memory`
**Worktree:** `.claude/worktrees/task2-verify3-memory`
**Date:** 2026-05-09
**Auditor:** independent (blind-audit constraint — did not read prior
verify reports)
**Scope:** `src/memory/{mmu,ram,rom,contention}.{cpp,h}` plus
cross-cutting NR-write dispatch in `src/core/emulator.cpp`. VHDL oracle
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`.

---

## Verdict

**NEW FINDINGS — 3 class-(a) bugs, all fixed on this branch.**

The audit deliberately focused on edge-case / boundary / differential
angles (per the prompt) rather than re-walking the surface area covered
by the first two passes. All three findings are subtle — they ride on
combinations of state (NR 0x08 unlock + readback; NR 0x50 RAM + NR 0x8C;
NR 0x50 = $FF + EFF7=1) that the prior passes did not exercise.

Tests pass clean: **mmu 202/180 PASS / 0 FAIL / 22 SKIP**, full ctest
**37/37 PASS**. No new regressions.

---

## Methodology

1. **Differential audit direction**: walked VHDL signals
   (`port_7ffd_reg(5)`, `nr_8c_altrom_*`, `MMU<i>` per
   `port_memory_change_dly` vs `nr_mmu_we`) and traced their C++
   counterparts. Looked for cases where the C++ helper updates ALSO
   touch state that VHDL leaves alone (clobber bugs) and vice-versa
   (forgetting to touch state VHDL touches).
2. **Multi-state interactions**:
   - NR 0x08 bit 7 unlock + subsequent MF+3 readback at 0x7xxx /
     Pentagon-1024 bank composition.
   - NR 0x50/0x51 with explicit RAM page (v < 0xE0) followed by NR 0x8C
     altrom-lock change.
   - NR 0x50/0x51 with v=0xFF under EFF7 RAM-at-0x0000 mode (eff7=1).
   - NR 0x8E with bit-3 / bit-2 / bit-0 boundary combinations.
3. **Boundary inputs** for every NR memory-related write: 0x00, 0x01,
   0x05 (RAM page), 0x0A/0x0B/0x0E (bank5/bank7 special pages — VHDL
   `mmu_A21_A13` shift-bypass), 0x80, 0xDF (just-below ROM threshold),
   0xE0 (ROM threshold start), 0xFE (just-below sentinel), 0xFF (ROM
   sentinel).
4. **Reset interactions**: confirmed `Mmu::reset(bool)` clears all
   reset-domain state (port_7ffd, port_1ffd, port_dffd, port_eff7,
   paging_locked_, contention_disabled_, l2 latches, port_1ffd_special_old).
   nr_8c lo→hi nibble copy is correct.
5. **Read-back contracts**: walked NR 0x08 / NR 0x50–0x57 / NR 0x69 /
   NR 0x8E read paths; confirmed the live MMU register view is
   delivered (not stale `regs_[]` cache). Found one read-back contract
   broken: NR 0x50 with v=$FF + EFF7=1 was returning 0x00/0x01 instead
   of $FF.
6. **Power-on defaults** for every memory-related NR register: cross-
   checked with VHDL `signal ... := ...` defaults and reset clauses.
7. **+3 special paging table** (VHDL :4625-4632): re-verified the C++
   table by algebraic derivation from the bit-construction in VHDL.
   All four configs match.
8. **Cross-cutting write paths**: NR 0x8E with bit-3 = 0 / 1 / 2 / 4 / 8
   combinations vs VHDL `port_memory_ram_change_dly` gating of MMU6/7.
9. **Floating-bus latch** (zxnext.vhd:4498-4509): verified mirror in
   read()/write() is updated on slot_contended.

---

## Findings

### Finding 1 (class-a): `unlock_paging()` did not clear `port_7ffd_` bit 5

**VHDL** (zxnext.vhd:3654-3656):
```vhdl
elsif nr_08_we = '1' and nr_wr_dat(7) = '1' then
   port_7ffd_reg(5) <= '0';
```

A NR 0x08 write with bit 7 set unconditionally clears `port_7ffd_reg(5)`
in VHDL. This is the lock bit; clearing it drops `port_7ffd_locked` and
also alters two downstream consumers:

* **VHDL :4318** (Multiface MF+3 readback at 0x7xxx) returns the full
  `port_7ffd_reg` byte, including bit 5.
* **VHDL :3765** Pentagon-1024 mode bank composition uses bit 5 of
  `port_7ffd_reg` as `port_7ffd_bank(5)` when
  `nr_8f_mapping_mode_pentagon_1024_en='1'`.

**C++ (pre-fix)**:
```cpp
void unlock_paging() { paging_locked_ = false; }
```
Cleared the standalone `paging_locked_` flag but left
`port_7ffd_` bit 5 set. Two divergences:

* MF+3 readback at 0x7xxx returned the stale bit 5.
* Pentagon-1024 bank composition (`compose_bank_`) read bit 5 from
  `port_7ffd_` and produced bank-5 high when VHDL would produce 0.

**Fix** (mmu.h:570-583): `unlock_paging()` now also clears bit 5 of
`port_7ffd_`, matching VHDL exactly.

**Severity**: low in current boot path (NR 0x08 unlock is rare and the
two affected read paths — MF+3 readback under enabled MF and
Pentagon-1024 — are not tested in the boot trajectory). But the
divergence is real and silent.

---

### Finding 2 (class-a): `set_nr_8c()` clobbered slots 0/1 even when explicitly RAM-mapped

**VHDL** (zxnext.vhd:2256-2257, :3813, :4619-4646):

```vhdl
elsif nr_8c_we = '1' then
   nr_8c_altrom <= nr_wr_dat;       -- store the 8-bit register
```

NR 0x8C does **NOT** trigger `port_memory_change_dly` (the OR-chain at
:3813 lists `port_7ffd_wr / port_1ffd_wr / port_dffd_wr / port_eff7_wr /
nr_8e_we / nr_8f_we_dly`, but NOT `nr_8c_we`). So `MMU<i>` is unchanged
on NR 0x8C — VHDL only updates `nr_8c_altrom` and the combinational
`sram_rom` / `sram_rom3` signals derived from it. The SRAM arbiter then
re-evaluates per-CPU-access using the new `sram_rom`, but only for
slots in legacy ROM mode (mmu_A21_A13(8)='1', i.e. MMU<i> >= 0xE0). For
slots explicitly RAM-mapped via NR 0x50/0x51 with v < 0xE0, VHDL routes
via `mmu_A21_A13(8)='0'` (line :3037-3043) — altrom does not engage,
RAM is served, `MMU<i>` is unchanged.

**C++ (pre-fix)**:
```cpp
void Mmu::set_nr_8c(uint8_t v) {
    nr_8c_reg_ = v;
    apply_legacy_rom_slots_();
}
```

`apply_legacy_rom_slots_()` unconditionally:
1. Sets `nr_mmu_[0]=nr_mmu_[1]=0xFF` (clobbering any explicit NR 0x50/0x51
   RAM page that user had written).
2. Calls `map_rom_physical(0/1, sram_rom*2 + i)` which sets
   `read_only_[0/1]=true` and re-points the cached read pointer to a
   ROM page.

**Failure scenario**:
1. User: `NR 0x50, 0x05` → slot 0 = RAM page 5. `nr_mmu_[0]=0x05`,
   `slots_[0]=0x05`, `read_only_[0]=false`.
2. User: `NR 0x8C, 0x80` (enable altrom). C++ now has `nr_mmu_[0]=0xFF`,
   `slots_[0]=sram_rom*2`, `read_only_[0]=true`. **CPU now reads ROM,
   not RAM page 5.** VHDL would still serve RAM page 5 (MMU0=0x05,
   mmu_A21_A13(8)=0).

**Fix** (mmu.cpp:517-538): per-slot refresh, gated on the slot
currently being in legacy ROM mode (`read_only_[i]==true`). Slots
explicitly RAM-mapped retain their NR-driven mapping.

**Severity**: medium-low — the boot trajectory does not appear to hit
this combination, but firmware that uses the standard "NR 0x50 to
remap RAM into slot 0/1 for inline data, then toggle altrom" pattern
would silently fail. The fix also has a positive performance side-
effect: NR 0x8C writes now do less work when the slots are already in
the right mode (the per-slot helper short-circuits on read-only=false).

---

### Finding 3 (class-a): `engage_legacy_rom_paging_slot` set wrong `nr_mmu_` value under EFF7=1

**VHDL** (zxnext.vhd:4636-4644 + :4686-4696):

The EFF7 RAM-at-0x0000 override (MMU0=0x00, MMU1=0x01) only fires when
`port_memory_change_dly='1'`. An explicit `NR 0x50, $FF` (or
`NR 0x51, $FF`) write goes through the `nr_mmu_we` path at :4686-4696,
which stores `nr_wr_dat` (= 0xFF) verbatim into MMU<i>. The eff7
override is bypassed because `port_memory_change_dly` is NOT pulsed by
nr_mmu_we (per VHDL :3813 — the OR-chain does not include nr_mmu_we).

**C++ (pre-fix)** (mmu.cpp engage_legacy_rom_paging_slot):
```cpp
if (port_eff7_reg_3_) {
    set_page(slot, static_cast<uint8_t>(slot == 0 ? 0x00 : 0x01));
    return;
}
```

`set_page(slot, 0x00)` sets `nr_mmu_[slot]=0x00`. So a subsequent
NR 0x50 read returns `0x00` — but VHDL would return `0xFF` (the
last value written via nr_mmu_we).

**Fix** (mmu.cpp:478-503): in the EFF7 branch, route the cached
read/write pointers to RAM page 0/1 (matching the SRAM arbiter's
intent) but keep `nr_mmu_[slot] = 0xFF` (matching VHDL's nr_mmu_we
verbatim store).

**Severity**: very low — requires the unusual combination of EFF7=1 +
NR 0x50/0x51,$FF. But the read-back contract is broken and a
debugger / save-state observer would see the wrong NR-port value.

---

## What was spot-checked (no findings)

The following areas were re-checked with edge-case angles and found
**clean / VHDL-faithful**:

* **+3 special paging table** (zxnext.vhd:4625-4632): algebraically
  derived all four (B,A) configs from VHDL bit-construction; matches
  C++ `apply_plus3_special_paging_` table 1:1.
* **NR 0x8E bit 3 = 0 / bit 2 = 0/1 combinations**: port_7ffd_reg(4)
  update gating by bit 2 matches; port_dffd_reg / port_1ffd_reg
  updates match; MMU6/7 suppression on bit 3 = 0 + no special-mode
  transition matches VHDL :3814 + :4677.
* **`port_1ffd_special_old` capture semantics** (zxnext.vhd:3720-3722,
  3728-3730, 3736-3738): VHDL captures _old only when
  `port_memory_change_dly=0` (between writes); else clause sets _old
  to '0' on idle. C++ models this as "set after each apply_paging_update_"
  which is functionally equivalent for canonical paths (CPU port
  writes are always preceded by idle clocks).
* **`compose_bank_`** (zxnext.vhd:3763-3766): Pentagon / Pentagon-1024 /
  standard composition matches; bank(6) forced 0 in Pentagon mode
  matches; bit-5 promotion in Pentagon-1024-en matches.
* **Reset semantics**: hard/soft reset both clear paging_locked_,
  port_7ffd_, port_1ffd_, port_dffd_reg, port_dffd_reg_6,
  port_eff7_reg_2/3, port_1ffd_special_old, l2 latches,
  contention_disabled_. nr_8c lo→hi nibble copy correct. NR 0x8F
  preserved across reset (no VHDL reset clause). Boot ROM re-enabled
  on reset if loaded.
* **`mmu_A21_A13` formula** (zxnext.vhd:2964): `to_sram_page` correctly
  shifts logical pages 0x00..0xDF → physical 0x20..0xFF; pages
  0xE0..0xFF wrap to 0x00..0x1F; bank5/bank7 special pages 0x0A/0x0B/0x0E
  bypass the shift to keep dual-port routing aligned.
* **NR 0x50–0x57 boundary at 0xE0**: C++ `rebuild_ptr` correctly
  detects `page >= 0xE0` for slots 0/1 (legacy ROM path) vs slots 2-7
  (inactive — both pointers null → reads return 0xFF). Matches VHDL
  arbiter at :3037 vs :3060-3061.
* **EFF7 reset clause** (:3777-3779): both bit 2 and bit 3 cleared on
  reset. C++ matches.
* **NR 0x69 bit 6 → port_7ffd_reg(3)** alias (zxnext.vhd:3658-3660):
  C++ `set_port_7ffd_bit3` correctly bypasses the lock gate. NR 0x69
  does NOT trigger `port_memory_change_dly` (VHDL :3813 OR-chain), and
  C++ correctly does not call `apply_paging_update_` on NR 0x69.
* **port_dffd_reg_6** separate flip-flop (zxnext.vhd:877, 3694, 4314):
  stored as a single bool, persisted in save_state, consumed by MF+3
  read mux at 0xDxxx. ✓
* **Profi mode forced off** (VHDL :3797): C++ correctly drops the Profi
  branch in compose_bank_, port_dffd_reg(3) clear gating, and the
  `port_dffd_reg(4)='1'` lock-bypass (effective_paging_locked never
  fires from Profi).
* **Boot ROM read priority**: highest above MF/DivMMC/L2/altrom/
  config-mode/normal — matches VHDL :1856.
* **Boot ROM 8 KB mirror**: `addr & 0x1FFF` indexing matches VHDL
  bootrom_mod hardwired to cpu_a(12:0).
* **Boot ROM size validation**: zero-pad / truncate semantics with
  diagnostic on mismatch — matches the VHDL hardwired 8 KB invariant.
* **Layer 2 read-over / write-over**: bank composition matches VHDL
  :2966-2969; `to_sram_page` shift applied identically to read and
  write paths.
* **NR 0x8C bit-3:0 lo→hi copy on hard reset** (VHDL :2253-2256): C++
  copies `nr_8c_reg_ = (lo<<4) | lo` correctly.
* **Save/load state**: all reset-domain state round-trips, including
  port_dffd_reg_6, port_1ffd_special_old, slot_contended_[],
  l2_segment_raw_, l2_offset_, l2_shadow_bank_.
* **`p3_floating_bus_dat` latch** (VHDL :4498-4509): C++ updates on
  every `slot_contended_[addr>>14]==true` access (read or write).
* **Contention model gates** (VHDL :4481, :4489-4493, :4496):
  i_contention_en composition (NR 0x08 b6, machine_timing_pentagon,
  cpu_speed) correct; mem_contend per-machine page decode correct;
  port_contend even-port + ULA+ correct (port_7ffd_active term
  documented as Phase B gap, not a new finding).

---

## Convergence assessment

**Audit has likely converged in spirit but not yet in coverage.** The
three findings here are corner cases that escaped both prior passes
because the prior passes focused on the canonical control flow (reset
→ paging port writes → CPU access). The findings sit at the
intersection of multiple subsystems:

* Finding 1: NR 0x08 (peripheral 3) interacting with port 0x7FFD
  (paging) interacting with MF+3 readback (peripheral) and Pentagon-
  1024 (NR 0x8F).
* Finding 2: NR 0x50/0x51 (MMU slot map) interacting with NR 0x8C
  (altrom).
* Finding 3: NR 0x50/0x51 + 0xFF sentinel + EFF7 (RAM-at-0x0000) all
  in flight together.

Other audit angles I touched but did not exhaust:

* NR 0x82–0x84 / 0x85–0x89 expbus port-enable masking — not modeled in
  jnext (documented gap); didn't dig into edge cases here.
* Profi mode — forced off in VHDL (:3797), so the dead-code branches
  in C++ are intentionally inert. No new findings.
* Pentagon-512 vs Pentagon-1024 vs standard transitions during port
  writes — verified compose_bank_ but did not exhaustively
  cross-product with EFF7(2) state.
* NR 0x82 bit-1 gating of the port_1ffd direct write on Next mode —
  flagged as G57 gap by `current_rom_bank()` comment, not a new
  finding.

**My belief**: a fourth pass that focuses specifically on
**transition-edge cases** (writes that change a state bit + the very
next CPU access that consumes it) would likely find more class-(b)
caching-staleness issues, but no more class-(a) bugs. The findings here
all happened because state was left stale across an event boundary that
the C++ helper didn't anticipate.

---

## Open questions

1. **Should `apply_legacy_rom_slots_()` itself become per-slot?** The
   current helper rebuilds both slots together, which is correct for
   the paging-trigger context (where VHDL :4619-4644 always rewrites
   both MMU0 and MMU1). But its callers from set_nr_8c (now per-slot)
   highlight that the function is awkward — a future cleanup could
   collapse to a single per-slot helper everywhere. Out of scope for
   this audit.
2. **Test coverage for findings 2 and 3**: a follow-up should add
   mmu_test cases:
   * `NR 0x50, 0x05` → check `read(0x0000)` returns RAM page 5 byte;
     write `NR 0x8C, 0x80`; check `read(0x0000)` STILL returns RAM page
     5 byte (not ROM).
   * `port_eff7_reg_3 = true; NR 0x50, 0xFF`; check `get_page(0)` returns
     `0xFF` (not 0x00); check `read(0x0000)` returns RAM page 0 byte.
3. **Pass-3 fix verification via boot run**: ideally re-run the
   NextZXOS boot trajectory headlessly to confirm no behaviour
   regression. Out of scope per the prompt's no-instrumentation rule.

---

## Test status

```
mmu_test:   202 / 180 PASS / 0 FAIL / 22 SKIP   (unchanged)
ctest --test-dir build:    37 / 37 PASS         (unchanged)
fuse_z80_tests:            PASS                 (unchanged)
contention_tests:          PASS                 (unchanged)
```

No regressions. All 22 SKIPs are pre-existing G12/G33-G37 plumbing
gaps unrelated to memory routing.

---

## Branch HEAD

After fixes, on `task2/verify3-memory`. To see the verification
commit, `git log --oneline -3`.
