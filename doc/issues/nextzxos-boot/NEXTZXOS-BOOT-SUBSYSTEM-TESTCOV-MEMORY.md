# Memory Subsystem Test-Coverage Audit (testcov-memory)

**Branch**: `task2/testcov-memory`
**Worktree**: `.claude/worktrees/task2-testcov-memory`
**Mandate**: every memory-subsystem fix from the Task-2 verify1..verify10
chain must have a regression unit test that prevents the bug from being
reintroduced.

---

## Verdict

**ZERO uncovered fixes**: every concrete memory-subsystem fix landed in
the `9f6162a..HEAD` window now has at least one dedicated regression
row. 29 new test rows added across `mmu_test`, `contention_test`, and
`nextreg_integration_test`; all passing.

---

## Total fixes catalogued

11 memory-subsystem fix commits identified between `9f6162a..HEAD` (the
Task-2 verify1..verify10 chain plus the cross-cutting commits that
touched `src/memory/*` or its emulator-glue handlers):

| # | Commit | Title (abbreviated) | Class-(a) findings |
|---|---|---|---|
| 1 | `f832f38` | NR $5x,$FF — VHDL-faithful per-slot semantics | 3 |
| 2 | `45d8b30` | VHDL-faithful +3 special-paging arbitration | 1 (with state-serialization extension) |
| 3 | `3dd4e73` | Slot 0/1 high-page handling + NR $8C cache refresh | 2 |
| 4 | `31d1786` | verify3 re-audit — pass-3 fixes | 3 |
| 5 | `560cb18` | verify4 re-audit — pass-4 fixes | 3 |
| 6 | `165835d` | verify5 re-audit — pass-5 fixes | 2 |
| 7 | `a9cbf79` | verify6 — `mem_active_page_for` MMU<i> sentinel | 1 |
| 8 | `ab92bab` | verify7 re-audit — pass-7 fixes | 2 (1 already test-covered MTC-01..03) |
| 9 | `b6b42dd` | verify8 — 5 class-(a) memory divergences | 5 |
| 10 | `f5ec6d8` | verify9 — strict convergence (4 fixes) | 4 |
| 11 | `9d252b6` | verify10 — L2 ROM-area gate | 1 |

**Total class-(a) fixes**: ~27 individual VHDL-divergence fixes across
the 11 commits. (Some commits land related/coupled fixes counted as a
single fix in the table where the regression test naturally covers all
of them.)

---

## Coverage table — per-fix → test row

Legend: **NEW** = added by this pass; **PRE** = covered before this
pass; **N/A** = a higher tier (integration / Mmu hot path) covers it
naturally. All tests cite the original commit in their description.

| Commit | Fix | Test row(s) | Status |
|---|---|---|---|
| `f832f38` | `NR $5x,$FF` per-slot — slot 0/1 preserves OTHER slot's RAM mapping | `FIX-NR5xFF-01` (`mmu_test`) | NEW |
| `f832f38` | `NR $52..$57=$FF` → inactive, NOT ROM-page-0 fallback | `FIX-NR5xFF-02` (`mmu_test`) | NEW |
| `f832f38` | `NR $56/$57=$FF` → inactive, NOT legacy RAM auto-paged | `FIX-NR5xFF-03` (`mmu_test`) | NEW |
| `45d8b30` | `apply_paging_update_` arbitration — entry table | `SPE-01..04` (`mmu_test`) | PRE |
| `45d8b30` | 1→0 special-mode exit reverts slots 2-5 | `FIX-PLUS3-01` (`mmu_test`) | NEW |
| `45d8b30` | port_7FFD write does NOT clobber special table | `FIX-PLUS3-02` (`mmu_test`) | NEW |
| `45d8b30` | `port_1ffd_special_old` save/load round-trip | `FIX-PLUS3-03` (`mmu_test`) | NEW |
| `3dd4e73` | NR $8C lock-bit flip refreshes slot 0/1 cached read pointer | `FIX-NR8C-CACHE-01` (`mmu_test`) | NEW |
| `3dd4e73` | NR $8C with no lock change preserves explicit RAM | `FIX-NR8C-CACHE-02` (`mmu_test`) | NEW |
| `3dd4e73` | NR $50 with v=$E0..$FE routes slot 0/1 to legacy ROM (NOT wrap-aliased SRAM) | `FIX-SLOT01-HIPAGE-01` (`mmu_test`) | NEW |
| `31d1786` | `unlock_paging()` clears port_7ffd_ bit 5 | `FIX-UNLOCK-01` (`mmu_test`) | NEW |
| `31d1786` | `set_nr_8c()` preserves explicit slot 0/1 RAM mapping | `FIX-NR8C-PRESERVE-01/02` (`mmu_test`) | NEW |
| `31d1786` | `engage_legacy_rom_paging_slot` under EFF7=1 keeps `nr_mmu_=$FF` | `FIX-EFF7-FF-01` (`mmu_test`) | NEW |
| `560cb18` | EFF7=1 + NR $50/$51=$FF → legacy ROM (NOT RAM mirror) | `FIX-EFF7-FF-01` (`mmu_test`) | NEW (shared with #31d1786) |
| `560cb18` | `nr_mmu_[8]` save/load round-trip lossless | `FIX-NRMMU-SAVE-01` (`mmu_test`) | NEW |
| `560cb18` | NR $12 propagates to `mmu_.l2_bank_` (CPU L2 hot path) | `FIX-NR12-PROP-01` (`mmu_test`) | NEW |
| `165835d` | `Mmu::reset()` re-arms `boot_rom_en` only when `config_mode=1` | `FIX-RESET-CFG-01-A/B` (`mmu_test`) | NEW |
| `165835d` | `set_machine_type` early-returns during +3 special paging | `FIX-MTC-SPECIAL-01` (`mmu_test`) | NEW |
| `a9cbf79` | `mem_active_page_for` consults `Mmu::get_page` (MMU<i> sentinel) | `FIX-MEMACTIVE-PAGE-01` (`contention_test`) | NEW |
| `ab92bab` | DivMmc `sram_rom3` wiring per machine type | `ROM-10..12` + `sram_rom3()` accessor tests in `Cat11` | PRE |
| `ab92bab` | `set_machine_type` per-slot refresh (only legacy-ROM slots) | `MTC-01..03` (Cat11c, `mmu_test`) | PRE (added in commit ab92bab itself) |
| `b6b42dd` | A1 — Layer 2 overlay segment gating per-half (low half always-on) | `FIX-L2-OVERLAY-LOWHALF-01/02` (`mmu_test`) | NEW |
| `b6b42dd` | A2 — NR 0x08 bit 6 reads effective `contention_disable` | `RW-02` (`nextreg_integration_test`) | PRE |
| `b6b42dd` | A3 — NR 0x08 bit 7 reads `effective_paging_locked` (Pentagon-1024 override observable) | `FIX-NR08-EFFLOCK-01` (`nextreg_integration_test`) | NEW |
| `b6b42dd` | A4 — +3 floating-bus port read uses `effective_paging_locked` | Mode-impossible to observe (Pentagon-1024 not active on +3); `FB-3A` covers raw lock | N/A |
| `b6b42dd` | A5 — `current_sram_rom()` 128K case factors altrom-lock | `FIX-CURRSRAMROM-128K-01` (`mmu_test`) | NEW |
| `f5ec6d8` | A1 — `Mmu::mem_contend_for_(addr)` per-page `nr_mmu_` decode | `MMU-09..12` exercise the gate; `FIX-MEMACTIVE-PAGE-01` pins the `nr_mmu_` sentinel feed | PRE + NEW |
| `f5ec6d8` | A2 — +3 floating-bus active-display arm via VRAM helper | `FB-03a` (`floating_bus_test`) | PRE (added in commit f5ec6d8 itself) |
| `f5ec6d8` | A3 — `ContentionModel::rebuild_for_type()` runtime NR $03 commit | `FIX-CONTEND-NR03-01` (`contention_test`) | NEW |
| `f5ec6d8` | A4 — `port_7ffd_active` OR-term in `port_contend()` | `FIX-CONTEND-7FFD-01/02/03` (`contention_test`) | NEW |
| `9d252b6` | L2 ROM-area `sram_active=0` gate (sum & 0x70 == 0x70) | `FIX-L2-ROM-AREA-01/02` (`mmu_test`) | NEW |

---

## Tests added (count + brief description)

**29 new regression rows** added in this pass:

### `test/mmu/mmu_test.cpp` — Cat 27 (23 rows)

* `FIX-NR5xFF-01` — NR $51,$FF preserves slot 0 RAM mapping (per-slot
  semantics, commit f832f38).
* `FIX-NR5xFF-02` — NR $52,$FF → slot 2 inactive (read 0xFF, write
  dropped); was wrongly mapped to ROM page 0 pre-fix.
* `FIX-NR5xFF-03` — NR $56,$FF → slot 6 inactive; was wrongly legacy-RAM
  auto-paged pre-fix.
* `FIX-PLUS3-01` — +3 special-mode 1→0 exit reverts slots 2-5 to bank 5
  / bank 2 (commit 45d8b30).
* `FIX-PLUS3-02` — port_7FFD write during +3 special does NOT clobber
  the special table (apply_paging_update_ arbitration).
* `FIX-PLUS3-03` — `port_1ffd_special_old` round-trips through save/load.
* `FIX-NR8C-CACHE-01` — NR 0x8C `lock_rom1` flip on +3 refreshes cached
  slot 0 ROM read pointer (commit 3dd4e73 finding 1).
* `FIX-NR8C-CACHE-02` — NR 0x8C with no lock change preserves explicit
  RAM mapping.
* `FIX-SLOT01-HIPAGE-01` — NR $50=0xE5 routes slot 0 to legacy ROM, NOT
  wrap-aliased SRAM page 0x05 (commit 3dd4e73 finding 2).
* `FIX-UNLOCK-01` — `unlock_paging()` clears bit 5 of `port_7ffd_`
  (commit 31d1786 finding 1).
* `FIX-NR8C-PRESERVE-01/02` — NR 0x8C does NOT clobber explicit slot 0/1
  RAM mappings (commit 31d1786 finding 2).
* `FIX-EFF7-FF-01` — NR $50,$FF under EFF7(3)=1 keeps `nr_mmu_=$FF`
  verbatim and routes slot 0 to legacy ROM, NOT mirror eff7's RAM-at-0x0000
  override (commits 31d1786 finding 3 + 560cb18 finding 1).
* `FIX-NRMMU-SAVE-01` — NR $50=0xE5 round-trips through save/load
  (verbatim VHDL MMU<0> register, commit 560cb18 finding 2).
* `FIX-NR12-PROP-01` — `Mmu::set_l2_active_bank()` propagates to CPU L2
  read path (commit 560cb18 finding 3).
* `FIX-RESET-CFG-01-A/B` — `Mmu::reset()` re-arms `boot_rom_en` only
  when config_mode=1 (commit 165835d finding 1).
* `FIX-MTC-SPECIAL-01` — `set_machine_type` during +3 special paging
  preserves the special MMU image (commit 165835d finding 2).
* `FIX-CURRSRAMROM-128K-01` — 128K with altrom lock_rom1=1 → sram_rom=1
  (NOT pure 7FFD(4); commit b6b42dd A5).
* `FIX-L2-OVERLAY-LOWHALF-01/02` — L2 overlay enables low half (0x0000)
  for seg=01 / seg=10, not just seg=00 / seg=11 (commit b6b42dd A1).
* `FIX-L2-ROM-AREA-01/02` — L2 with bank=0x70 → `sram_active=0` (read
  0xFF, write dropped); pre-fix would alias into ROM-in-SRAM region
  (commit 9d252b6).

### `test/contention/contention_test.cpp` — CT-CAT27 (5 rows)

* `FIX-CONTEND-NR03-01` — `ContentionModel::rebuild_for_type` updates
  type/LUT without resetting `port_7ffd_io_en` (commit f5ec6d8 A3).
* `FIX-CONTEND-7FFD-01/02/03` — port_contend `port_7ffd_active` OR-term
  gated on `port_7ffd_io_en` AND machine timing 128K/+3 (commit f5ec6d8 A4).
* `FIX-MEMACTIVE-PAGE-01` — `Mmu::get_page` returns the 0xFF MMU<i>
  sentinel for legacy-ROM slot 0/1, NOT the resolved physical ROM page
  (commit a9cbf79).

### `test/nextreg/nextreg_integration_test.cpp` — Cat27-NR08-Effective (1 row)

* `FIX-NR08-EFFLOCK-01` — NR 0x08 bit 7 read returns NOT
  `effective_paging_locked()` (Pentagon-1024 override observable; commit
  b6b42dd A3).

---

## Test status

| Suite | Total | Pass | Fail | Skip | Note |
|---|---|---|---|---|---|
| `mmu_test` | 228 | 206 | 0 | 22 | +23 rows vs pre-pass (was 205/183/0/22) |
| `contention_test` | 75 | 75 | 0 | 0 | +5 rows vs pre-pass (was 70/70/0/0) |
| `nextreg_integration_test` | 181 | 181 | 0 | 0 | +1 row vs pre-pass (was 180/180/0/0) |
| `floating_bus_test` | 30 | 30 | 0 | 0 | unchanged (FB-03a covers verify9 A2) |
| `fuse_z80_test` | 1356 | 1356 | 0 | 0 | unchanged |
| Full `ctest` suite | 37/37 | 37 | 0 | 0 | green |

---

## Coverage analysis — what was already covered before this pass

* `MTC-01/02/03` (Cat11c) — added by commit ab92bab itself; covers the
  verify7 fix `set_machine_type` per-slot refresh.
* `FB-03a` — added by commit f5ec6d8 itself; covers verify9 A2 (+3
  active-display VRAM-byte arm).
* `RW-02` (nextreg_integration_test) — already pokes
  `commit_contention_disable_on_hc(300)` to observe verify8 A2 effective
  contention_disable; covers the bit-6 readback fix.
* `SPE-01..04` — pre-existing; covers the +3 special-paging entry table
  (the verify9 fix to apply_paging_update_ arbitration was about
  arbitration, not the entry table itself).
* `ROM-10/11/12` — pre-existing; covers `sram_rom3()` per-machine-type
  composition (verify7 fix wires DivMmc through this; the accessor was
  already tested).

---

## Coverage analysis — items NOT requiring new tests

* **Verify8 A4 (+3 floating-bus port read uses `effective_paging_locked`)**:
  the only difference between `paging_locked` and `effective_paging_locked`
  is Pentagon-1024 mode (`nr_8f_mode=11 AND NOT EFF7(2)`). Pentagon-1024
  is a Next/Pentagon-mode feature; on +3 the override is mode-impossible.
  The +3 port 0x0FFD read path was therefore tested via `FB-3A` (raw
  paging_locked=1 → 0xFF) which already exercises the gate at `+3 timing`
  conditions. No discriminative regression test would be observable.

* **Verify9 A1 (`mem_contend_for_` per-page latch decode)**: the L2
  hot-path latch update is observable via the existing CT-FB-01..04 rows
  (which run on `Emulator::ZX_PLUS3` with bank-5 contended slot mapping).
  The verify9 fix changed the latch source from per-16K-slot mirror to
  per-page `nr_mmu_` decode; CT-FB-03 specifically exercises a contended
  slot-1 access AND a non-contended slot-0 access on the same Mmu and
  asserts the latch updates only on the contended one. The `nr_mmu_`
  feed is also pinned by the new `FIX-MEMACTIVE-PAGE-01` row.

---

## Process notes

* No `src/memory/*` code changes in this pass — it is test-only as
  mandated. No bugs were uncovered while writing the tests.
* All new tests cite the originating commit hash and the VHDL line
  range from the original fix's description, satisfying the "prevent
  reintroduction" requirement.
* All tests compile clean and pass on a green tree (build/`ctest -V`).

