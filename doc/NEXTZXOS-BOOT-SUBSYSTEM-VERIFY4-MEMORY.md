# Pass-4 Memory Subsystem Verification Re-Audit

**Branch**: `task2/verify4-memory`
**Worktree**: `.claude/worktrees/task2-verify4-memory`
**Verdict**: **NEW FINDINGS — 3 class-(a) bugs found and fixed**
**Convergence**: NOT YET CONVERGED (pass-4 still surfaced bugs that escaped passes 1-3)
**Tests**: `mmu_tests` 202/180/0/22 — all pass; `fuse_z80_tests` 1356/1356; `contention_tests` pass; full ctest suite 37/37 pass.

## Methodology

Pass-4 narrowed the lens onto angles less covered by passes 1-3:

1. **Transition-edge stale-cache audit**. For every paging-affecting NR/port write, asked: does the cached `read_ptr_[]` / `write_ptr_[]` / `slots_[]` / `nr_mmu_[]` / `l2_bank_` / `l2_shadow_bank_` get re-derived synchronously, or only on the next paging-port write? VHDL is combinational — every signal feeding `mmu_A21_A13` or `layer2_active_bank` should propagate immediately.

2. **Reset-state matrix**. Walked each Mmu state field × each reset path (hard / soft / `Mmu::reset(true/false)`). Cross-checked against VHDL `if reset='1' then ...` clauses (zxnext.vhd:1730 wires `reset` to `reset_hard OR reset_soft`, so all `reset='1'` cases fire on both).

3. **Save/load full coverage audit**. Walked `Mmu::save_state` / `Mmu::load_state` line-by-line; listed every Mmu state field; checked round-trip semantics against VHDL behaviour.

4. **Differential VHDL signal coverage**. Spot-checked signals not previously cited in fix commits:
   - `sram_pre_romcs_replace`, `sram_romcs_en`, `sram_mem_hide_n` (all expansion-bus features, jnext does not model expbus — tied false, OK).
   - `sram_pre_bank5`, `sram_pre_bank7`, `mem_active_bank5`, `mem_active_bank7` — dual-port VRAM exemption at pages `0x0A`/`0x0B`/`0x0E`. jnext models this via `Mmu::to_sram_page()` exemption list. Verified consistent.
   - `port_dffd_reg_6_` — Multiface +3 readback only; stored, never consumed by paging. OK.
   - `bootrom_en` reset semantics — jnext reseeds in `Mmu::reset()` (line 132); soft-reset path saves/restores explicitly in `Emulator::soft_reset` (line 5032/5048). OK.

5. **NR-write dispatch audit**. Reviewed every NR write handler in `Emulator::install_port_handlers` for paging-related side-effects.

## Findings

### Finding 1 (class-a): EFF7(3) RAM-at-0x0000 incorrectly applied on `NR $50,$FF` / `NR $51,$FF` writes

**Location**: `src/memory/mmu.cpp` `Mmu::engage_legacy_rom_paging_slot()` (was lines 480-501) and `Mmu::rebuild_ptr()` page≥0xE0 slot 0/1 branch (was lines 207-219).

**VHDL contract**: `zxnext.vhd:4636-4644` — the EFF7(3) RAM-at-0x0000 override (forces MMU0=0x00, MMU1=0x01) fires only inside the `port_memory_change_dly = '1'` branch at line 4619. An explicit NR 0x50/0x51 write goes through the **separate** `nr_mmu_we = '1'` branch at lines 4686-4699, which stores `MMU<i> <= nr_wr_dat` verbatim. Per `zxnext.vhd:3813`, NR 0x50-0x57 writes do NOT contribute to `port_memory_change_dly`, so the eff7 override does NOT activate on the NR-write cycle.

**Pre-fix bug**: when `port_eff7_reg_3_=1` and the user wrote `NR $50,$FF` or `NR $51,$FF`, jnext routed slot 0/1's cached `read_ptr_/write_ptr_` to RAM page 0/1 immediately. VHDL leaves the slot in legacy ROM mode (sram_rom-derived) until the NEXT paging-port write asserts the eff7 override.

**Fix**: removed the eff7 branch from both `engage_legacy_rom_paging_slot()` and the `rebuild_ptr` slot 0/1 high-page branch. They now unconditionally route to legacy ROM (sram_rom*2 + slot), which is the correct VHDL behaviour for the `nr_mmu_we` path.

**Practical impact**: small. Real firmware almost always follows an NR 0x50/0x51 = 0xFF write with a paging-port write (or never sets EFF7(3)). But the divergence is unambiguous against VHDL.

### Finding 2 (class-a): `nr_mmu_[8]` not persisted in `save_state`/`load_state`; verbatim 0xE0..0xFE NR 0x50/0x51 values lost on round-trip

**Location**: `src/memory/mmu.cpp` `Mmu::save_state` / `Mmu::load_state`.

**VHDL contract**: `zxnext.vhd:4686-4699` stores the verbatim NR-write data into `MMU<i>`, including values in the 0xE0..0xFE range (which gate the slot through the `mmu_A21_A13(8)='1'` ROM-area branch). The NR-port read-back at `zxnext.vhd:6075-6082` returns `MMU<i>` verbatim.

**Pre-fix bug**: `save_state` did not write the `nr_mmu_[8]` array. `load_state` recovered it via `nr_mmu_[i] = read_only_[i] ? 0xFF : slots_[i]`, which collapses any verbatim 0xE0..0xFE write back to the 0xFF sentinel (because `read_only_[i]=true` for high-page values via `rebuild_ptr`'s slot 0/1 branch). Subsequent `NR $50/$51` reads would return 0xFF instead of the actual stored 0xE0..0xFE value.

**Fix**: appended `w.write_bytes(nr_mmu_, 8)` to `save_state` and `r.read_bytes(nr_mmu_, 8)` to `load_state` (after the existing `port_1ffd_special_old_` field). Older save streams that predate this addition will short-read; `StateReader`'s bounds check catches them.

**Practical impact**: small. Verbatim 0xE0..0xFE NR 0x50/0x51 writes are unusual; firmware typically writes either a low-page RAM mapping or 0xFF. But the round-trip is now lossless.

### Finding 3 (class-a): NR $12 (Layer 2 active bank) write does not propagate to `Mmu::l2_bank_`; CPU L2 map uses stale bank

**Location**: `src/core/emulator.cpp` NR 0x12 write handler (was line 681-684).

**VHDL contract**: `zxnext.vhd:2968` —
```
layer2_active_bank <= nr_12_layer2_active_bank when port_123b_layer2_map_shadow = '0'
                 else nr_13_layer2_shadow_bank;
```
This is **combinational** — any write to `nr_12_layer2_active_bank` (NR 0x12) updates `layer2_active_bank` for the next CPU memory cycle. The CPU L2 read/write-over path at `zxnext.vhd:3077` then uses the live `layer2_active_bank` to address SRAM.

**Pre-fix bug**: Mmu's L2 read path (mmu.h:255, 366) reads `l2_bank_` for the bank component when `l2_map_shadow_=0`. `l2_bank_` is updated only by `Mmu::set_l2_port` (called from the port 0x123B handler) — NOT by NR 0x12 writes. So an NR 0x12 write between two 0x123B writes left the cached bank stale; CPU L2 map continued using the previous bank.

NR 0x13 has the analogous propagation via `mmu_.set_l2_shadow_bank()` (already at emulator.cpp:700). NR 0x12 was the missing seam.

**Fix**: added `Mmu::set_l2_active_bank(uint8_t)` accessor in `mmu.h` that writes `l2_bank_`; the NR 0x12 write handler now calls `mmu_.set_l2_active_bank(layer2_.active_bank())` after `layer2_.set_active_bank(v)`.

**Practical impact**: small in the current boot path (firmware sequences NR 0x12 + port 0x123B together), but real for any firmware that re-banks Layer 2 mid-flight via a copper or NR 0x12 write — which is exactly the use-case the L2 CPU map exists for.

## Transition-edge audit summary (cleared)

For each NR/port write checked, verified the cache propagation is now VHDL-faithful:

| Write site | Cache field(s) refreshed | Mechanism |
|---|---|---|
| NR $50-$57 | nr_mmu_, slots_, read_ptr_, write_ptr_, read_only_ | `set_page` / `engage_legacy_rom_paging_slot` |
| NR $03 (machine type) | slot 0/1 read_ptr_ via current_sram_rom() | `set_machine_type` calls `apply_legacy_rom_slots_()` |
| NR $03 (config_mode) | implicit; read path checks config_mode_ dynamically | `mmu_.set_config_mode()` |
| NR $04 (romram_bank) | implicit; read path uses nr_04_romram_bank_ dynamically | `mmu_.set_nr_04_romram_bank()` |
| NR $07 (cpu_speed) | none required (contention only) | — |
| NR $08 (unlock paging) | port_7ffd_(5)=0; no MMU update per VHDL :3813 | `unlock_paging` (matches VHDL) |
| NR $0A (peripheral 5) | none required (DivMMC active checked dynamically) | — |
| NR $12 (L2 active bank) | l2_bank_ | NEW (Finding 3) |
| NR $13 (L2 shadow bank) | l2_shadow_bank_ | `mmu_.set_l2_shadow_bank()` |
| NR $69 b6 (shadow bit) | port_7ffd_(3) + ULA shadow flag | `mmu_.set_port_7ffd_bit3()` |
| NR $8C (altrom) | slots 0/1 read_ptr_ if read_only_ | `set_nr_8c` calls `engage_legacy_rom_paging_slot` |
| NR $8E (unified paging) | full apply_paging_update_() | `write_nr_8e` |
| NR $8F (mapping mode) | full apply_paging_update_() | `write_nr_8f` |
| port 0x7FFD | full apply_paging_update_() | `map_128k_bank` |
| port 0x1FFD | full apply_paging_update_() | `map_plus3_bank` |
| port 0xDFFD | full apply_paging_update_() | `write_port_dffd` |
| port 0xEFF7 | full apply_paging_update_() | `write_port_eff7` |
| port 0x123B | l2_bank_, l2_shadow_bank_, l2_offset_ etc. | `set_l2_port` |
| port 0xE3 (DivMMC) | none required (is_active() dynamic) | — |
| Multiface NMI | none required (is_mem_active() dynamic) | — |

## Reset-state matrix summary (verified)

Walked each Mmu state field × `reset(hard)` path. The current `reset()` ignores the `hard` flag (with a deliberate `(void)hard` and a long comment explaining why: VHDL's top-level `reset` signal is `reset_hard OR reset_soft`, so all `reset='1'` clauses fire on BOTH paths). Verified field-by-field that this matches VHDL:

- All paging-port shadow registers (port_7ffd_, port_1ffd_, port_dffd_reg_, port_dffd_reg_6_, port_eff7_*, paging_locked_, port_1ffd_special_old_) cleared per VHDL :3646-3779.
- nr_8c lo-nibble→hi-nibble copy applied per VHDL :2253-2256.
- nr_8f_mode_ NOT reset per VHDL :3787-3794 (no reset clause; survives both hard and soft).
- L2 latches (l2_*) cleared per VHDL :3907-3923.
- nr_04_romram_bank_ reset to 0 per VHDL :1104; config_mode left alone (Emulator-driven).
- bootrom_en re-enabled per VHDL :5110 (gated on config_mode in VHDL; jnext re-enables when `boot_rom_` is non-null and the Emulator soft-reset path saves/restores explicitly).
- Slot defaults seeded from `RESET_PAGES`; ROM physical pages 0/1 mapped via `map_rom_physical(0/1)`.

No discrepancies.

## Save/load full coverage audit

Built field-by-field table; every Mmu state field is now persisted, with the addition from Finding 2:

| Field | Persisted? | Notes |
|---|---|---|
| `slots_[8]` | ✓ | written |
| `nr_mmu_[8]` | ✓ NEW | Finding 2 fix; was lossy-derived |
| `read_only_[8]` | ✓ | written |
| `paging_locked_` | ✓ | written |
| `p3_floating_bus_dat_` | ✓ | written |
| `slot_contended_[4]` | ✓ | written |
| `contention_disabled_` | ✓ | written |
| `nr_8c_reg_` | ✓ | written |
| `machine_type_` | ✓ | written |
| `port_7ffd_`, `port_1ffd_`, `port_1ffd_special_old_` | ✓ | written |
| `port_dffd_reg_`, `port_dffd_reg_6_` | ✓ | written |
| `port_eff7_reg_2_`, `port_eff7_reg_3_` | ✓ | written |
| `nr_8f_mode_` | ✓ | written |
| Layer 2 state (8 fields) | ✓ | written |
| `config_mode_`, `nr_04_romram_bank_`, `rom_in_sram_` | ✓ | written |
| `boot_rom_en_` | ✓ | written |
| `boot_rom_buf_`, `boot_rom_`, `boot_rom_size_` | ✗ derivable | re-installed by Emulator on init |
| `read_ptr_[8]`, `write_ptr_[8]` | ✗ derivable | rebuild_ptr() called on load |

Cached pointer fields (`read_ptr_`, `write_ptr_`) are correctly derivable via `rebuild_ptr` after `slots_/read_only_/nr_mmu_` restore — `load_state` calls `rebuild_ptr(i)` for every slot. Verified the eff7 + high-page branch in `rebuild_ptr` now matches VHDL after Finding 1 fix.

## Convergence assessment

**NOT YET CONVERGED.** Pass-4 surfaced 3 class-(a) bugs that escaped 3 prior independent audits. Two were edge cases (Findings 1 + 2) involving rare register-value combinations; one was a missed cache-invalidation seam (Finding 3) that mirrors an analogous correct one for NR $13.

The pattern is consistent with diminishing returns but not yet zero — each pass surfaces fewer issues but they get more specific. Pass 5 may yet find another, particularly in:

- Inter-subsystem handoffs (e.g., NR $12 / Layer 2 / Mmu).
- Save/load round-trip of less-tested register-value combinations.
- Reset semantics of fields whose VHDL has no explicit reset clause.

A pass-5 audit narrowed onto these themes would be useful; if pass 5 returns ZERO findings, that's a strong convergence signal.

## Open questions / coverage gaps

- **`port_1ffd_special_old_` per-cycle timing fidelity**: VHDL's special_old reset to 0 in the else-branch (zxnext.vhd:3736-3738) is a per-clock-edge behaviour. jnext models it at per-write granularity. I traced through the special=1 → special=0 transition and verified equivalence at the granularity that matters (the MMU update fires at the right moment). But there could be a more pathological scenario (e.g., copper-driven NR 0x8E writes interleaved with port writes) where the per-cycle vs per-write granularity diverges. Out of scope for blind verification; would need a targeted live-trace.

- **`mmu_A21_A13` formula**: the `(0x0001 + page[7:5])` arithmetic is correct for pages 0x00..0xDF (gives 0x20..0xFF). For pages 0xE0..0xFF it overflows to 0x100..0x11F (bit 8 high, slot inactive). jnext's `to_sram_page()` simplifies to `page + 0x20` for non-exempt pages and skips 0x0A/0x0B/0x0E — verified consistent with VHDL within the documented range. The wrap behaviour for pages 0xE0..0xFE (which jnext now correctly handles via the `slot >= 2` inactive branch and the `slot 0/1` legacy-ROM branch in `rebuild_ptr`) is now fully VHDL-faithful per Finding 1 fix.

- **NR 0x82-0x84 reset_type semantics**: VHDL :5052-5057 reloads to 0xFF only when `nr_85_internal_port_reset_type='1'`. jnext models this in `NextReg::reset()` and `Emulator::soft_reset`. Not re-audited in pass 4 (covered by passes 1-3).

## Files Changed

- `src/memory/mmu.cpp` — Findings 1 (engage_legacy_rom_paging_slot, rebuild_ptr) and 2 (save/load nr_mmu_).
- `src/memory/mmu.h` — Finding 3 accessor `set_l2_active_bank`.
- `src/core/emulator.cpp` — Finding 3 NR 0x12 write handler.

## Test Status

```
Test project /home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify4-memory/build
1/37 fuse_z80_tests ...........  Passed
7/37 mmu_tests ................  Passed
37/37 contention_tests .........  Passed
...
100% tests passed, 0 tests failed out of 37
```

All 37 unit-test suites pass. mmu_test reports the canonical 202/180/0/22 (passed/skipped/failed/total) with zero new failures introduced by these fixes.
