# Pass-14 Memory Subsystem — Blind Audit Report

**Branch:** `task2/verify14-memory` (off integration HEAD `73c3146`)
**Auditor:** Pass-14 blind audit agent
**Date:** 2026-05-10
**Scope:** Memory subsystem of jnext (Task 2 boot-critical subsystem audit)

## ZERO FINDINGS — claiming convergence for memory subsystem (Pass-14)

After 13 prior passes (10 audit + 3 verify) and a retroactive test-coverage
wave, this Pass-14 blind audit could not surface any genuine class-(a/b/c)
finding in the memory subsystem. The areas scrutinized below were all checked
against the VHDL oracle and the existing C++ implementation, and every cited
behavior matches VHDL within the Pass-13 architectural envelope (the
remaining class-(d) items — half-cycle / `_q` registered signals — are
inherently architectural and out of scope for class-(a/b/c) iteration).

## Areas scrutinized

### 1. Cross-subsystem mirror exhaustive (Pass-13 angle)

Pass-13 found **NR 0x69 bit 7** missing its `mmu_.l2_enable_` mirror
(`V13-MEM-01`). Pass-14 systematically checked every NR/port field that has a
shadow copy in another subsystem, looking for analogous gaps:

| VHDL FF | Writers | Mirror sites checked | Status |
|---|---|---|---|
| `port_123b_layer2_en` | port 0x123B b1, NR 0x69 b7 | `Layer2::enabled_` + `Mmu::l2_enable_` | ✓ V13-MEM-01 covered |
| `port_7ffd_reg(3)` (shadow) | port 0x7FFD b3, NR 0x69 b6 | `Mmu::port_7ffd_(3)` + Ula's i_ula_shadow_en | ✓ both writers fan to both shadows |
| `port_7ffd_reg(5)` (lock) | port 0x7FFD b5, NR 0x08 b7 (clear) | `Mmu::paging_locked_` + `port_7ffd_(5)` | ✓ `unlock_paging()` keeps both in sync |
| `port_7ffd_reg(2:0)` (bank) | port 0x7FFD, NR 0x8E b3=1 (b6:4) | `Mmu::port_7ffd_(2:0)` | ✓ both writers update verbatim |
| `port_7ffd_reg(4)` (rom-hi) | port 0x7FFD, NR 0x8E b2=0 (b0) | `Mmu::port_7ffd_(4)` | ✓ both writers handled |
| `port_dffd_reg(0)` | port 0xDFFD, NR 0x8E b3=1 (b7) | `Mmu::port_dffd_reg_(0)` | ✓ both writers handled |
| `port_dffd_reg(2:1)` | port 0xDFFD, NR 0x8E b3=1 (clear) | `Mmu::port_dffd_reg_(2:1)` | ✓ both writers handled |
| `port_dffd_reg(3)` | port 0xDFFD, NR 0x8E b3=1 (clear) | `Mmu::port_dffd_reg_(3)` | ✓ both writers handled |
| `port_1ffd_reg(2:0)` | port 0x1FFD, NR 0x8E b3=1 | `Mmu::port_1ffd_(2:0)` | ✓ both writers handled |
| `port_ff_reg(5:0)` | port 0xFF, NR 0x69 b5:0 | `Emulator::port_ff_reg_` | ✓ both writers handled |
| `port_ff_reg(6)` (int_disable) | port 0xFF, NR 0x22 b2, NR 0xC4 NOT(b0) | shadow `ula_int_disabled_` | ✓ V12-NMP-02/V12-MEM mirrored all 3 |
| `nr_22_line_interrupt_en` | NR 0x22 b1, NR 0xC4 b1 | `VideoTiming::line_interrupt_enable` | ✓ V12 mirrored |
| `nr_12_layer2_active_bank` | NR 0x12 | `Layer2::active_bank_` + `Mmu::l2_bank_` | ✓ NR 0x12 handler updates both; port 0x123B passes layer2's value into Mmu |
| `nr_13_layer2_shadow_bank` | NR 0x13 | `Layer2::shadow_bank_` + `Mmu::l2_shadow_bank_` | ✓ NR 0x13 handler updates both |
| `nr_8c_altrom` | NR 0x8C | `Mmu::nr_8c_reg_` + `Rom::alt_rom_*` | ✓ NR 0x8C handler updates both |
| `nr_03_machine_type` | NR 0x03 b2:0 (gated config_mode) | `Mmu::machine_type_` + `NextReg::nr_03_machine_type_` | ✓ NR 0x03 handler updates both; ContentionModel rebuild on type change |
| `nr_04_romram_bank` | NR 0x04 | `NextReg::nr_04_romram_bank_` + `Mmu::nr_04_romram_bank_` | ✓ NR 0x04 handler updates both |
| `nr_82-85` (port_io_en) | NR 0x82-0x85 | shadow in `ContentionModel::port_7ffd_io_en_`; consumed via `nextreg_.cached(0x82)` | ✓ NR 0x82 handler refreshes contention shadow; reset paths handle reset_type_1 |

**No analogous gap found.** Every multi-writer FF I could identify in the
VHDL is properly mirrored to all consumers in the C++.

### 2. Port read-back via Mmu paths

Verified that every memory-related port read-back surfaces the live state:
- **Port 0x123B read** (`Mmu::l2_port_readback`) composes `seg(7:6) | shadow(3) | rd_en(2) | l2_enable_(1) | wr_en(0)` — matches VHDL :3933 verbatim.
- **NR 0x50–0x57 read** (`Mmu::get_page`) returns `nr_mmu_[slot]` verbatim — matches VHDL :6075-6082 / :4686-4699 (nr_mmu_we writes verbatim).
- **NR 0x12/0x13 read** returns `Layer2::active_bank() & 0x7F` / `Layer2::shadow_bank() & 0x7F` — matches VHDL :5930-5931 7-bit mask.
- **NR 0x8C read** returns `Mmu::get_nr_8c()` — matches VHDL :6156 (full 8-bit).
- **NR 0x8E read** returns `Mmu::read_nr_8e()` composition — matches VHDL :6158-6159.
- **NR 0x8F read** returns `Mmu::nr_8f_mode() & 0x03` — matches VHDL :6162.
- **NR 0x69 read** composes `layer2_.enabled() | mmu_.shadow_screen_en() | port_ff_reg_(5:0)` — matches VHDL :6095-6096.
- **NR 0x08 read** composes `(NOT effective_paging_locked) | contention_disabled_ | nr_08_stored_low_(5:0)` — matches VHDL :5906 (Verify8/Verify12).
- **Port 0x0FFD (+3 floating bus)** — uses `effective_paging_locked()` + `ula_floating_bus_active_arm()` + `mmu_.p3_floating_bus_dat()` — matches VHDL :4517 + zxula.vhd:573 (Verify9).

### 3. VHDL `*_q` registered signals

All identified `_q`/`_d` patterns are class-(d) territory (architectural,
listed in prior passes' aggregate report). The only one in the memory
subsystem is `port_7ffd_dat <= port_7ffd_reg` on falling CLK_CPU edge
(VHDL :3678-3681). The C++ collapses this into the `port_7ffd_` field with
per-instruction granularity — this is pre-existing class-(d) and does not
re-surface as a class-(a/b/c) issue at this granularity. Same for
`port_1ffd_special_old` (already saved/loaded per Verify4).

### 4. Save/load round-trips

Verified `Mmu::save_state` / `Mmu::load_state` covers every persistent
memory-subsystem field:
- `slots_`, `read_only_`, `nr_mmu_` (verbatim) — Verify4 fix
- `paging_locked_`, `port_7ffd_`, `port_1ffd_`, `port_1ffd_special_old_` — Verify4
- `port_dffd_reg_`, `port_dffd_reg_6_`, `port_eff7_reg_2_`, `port_eff7_reg_3_`
- `nr_8f_mode_`, `nr_8c_reg_`, `nr_04_romram_bank_`, `config_mode_`, `rom_in_sram_`, `boot_rom_en_`
- `contention_disabled_`, `machine_type_`
- `l2_*` (write/read enable, segment_raw, segment_mask, bank, shadow_bank, enable, map_shadow, offset)
- `p3_floating_bus_dat_`, `slot_contended_`

`Emulator::load_state` re-pushes ContentionModel state from canonical
loaded NextReg / Mmu fields:
- `contention_.rebuild_for_type(mmu_.machine_type())`
- `contention_.set_cpu_speed(cs07)`
- `contention_.set_pending_cpu_speed(cs07)`
- `contention_.set_contention_disable(mmu_.contention_disabled())` (sets BOTH effective and shadow)
- `contention_.set_port_7ffd_io_en((nextreg_.cached(0x82) & 0x02) != 0)`

This is `V12-MEM-02` discriminative-tested — no gap found.

### 5. Default values on cold boot vs warm boot vs config-mode reset

Per VHDL :4595-4700, the MMU register process resets to specific defaults
on reset, and is updated on `port_memory_change_dly`/`nr_mmu_we`/initial
power-on. The C++ `Mmu::reset(hard)` correctly:
- Seeds `slots_[]` and `nr_mmu_[]` to the VHDL :4611-4618 defaults (0xFF/0xFF/0x0A/0x0B/0x04/0x05/0x00/0x01).
- Clears `paging_locked_`, `contention_disabled_`, `port_dffd_reg_*`, `port_eff7_reg_*`, `port_7ffd_/1ffd_/1ffd_special_old_`.
- Per VHDL :2253-2256, performs the `nr_8c` lo→hi nibble copy on reset (the only memory-subsystem reset behavior that's NOT a `:= 0` clear).
- Per VHDL :3787-3794, leaves `nr_8f_mode_` alone on reset (no reset clause in VHDL).
- Per VHDL :5109-5111, re-enables boot ROM only when `config_mode_=1` at reset time.
- Skips `apply_legacy_paging_()` because the seed already produces the correct state with all paging registers cleared.

`hard` parameter is currently unused (post-G46(b) correction noted that VHDL's `reset` is `reset_hard OR reset_soft` so every "if reset='1'" clause fires on both — confirmed in audit).

### 6. NR 0x03 machine_type transitions

Verified that on a NR 0x03 commit (gated on `config_mode='1'` per VHDL :5137):
- `Mmu::set_machine_type(new_mt)` rebuilds slot 0/1 if they're in legacy ROM mode (Verify7) — preserves explicit RAM-mapped slots from prior NR 0x50/0x51 writes.
- `divmmc_.set_rom3_active(mmu_.sram_rom3())` repushes the new sram_rom3 state.
- `contention_.rebuild_for_type(new_mt)` rebuilds the LUT and per-machine bank decode (Verify9) while preserving dynamic gate state (cpu_speed, contention_disable, mem_active_page).

The C++ MachineType enum collapses VHDL's separate `nr_03_machine_type` and
`nr_03_machine_timing` fields into one — this is pre-existing
class-(d) territory (would require a separate timing enum on
ContentionModel + NR 0x03 dispatcher refactor). In practice, real firmware
sets timing and type together so this divergence is unobservable in any
production boot path. Not surfacing as a class-(a/b/c) finding.

### 7. Multi-overlay precedence

Verified the priority chain in `Mmu::read()` and `Mmu::write()` matches
VHDL :2937-2945:
1. **Boot ROM** (highest, gated on `boot_rom_en_` AND `addr<0x4000`)
2. **Multiface** (gated on `addr<0x4000` AND `mf_overlay_active_()`)
3. **DivMMC** (gated on `addr<0x4000` AND `divmmc_->is_active()`)
4. **Layer 2 read/write-over** (gated on `l2_overlay_active_for(addr)`)
5. **Alt-ROM** (gated on `nr_8c_altrom_en()` AND read-only mode AND `!config_mode_` AND ROM-mapped slot)
6. **Config-mode SRAM routing** (gated on `config_mode_` AND ROM-mapped slot)
7. **Normal MMU** (default — `read_ptr_[slot]` / `write_ptr_[slot]`)

All gates checked against VHDL :3028-3132 priority arbiter. No divergence.

### 8. Layer 2 base address NR 0x12/0x13 + shadow base + segment combinations

Verified the `l2_overlay_active_for()` + `l2_offset_pre_for()` per-half
gate matches VHDL :3037-3066 / :2966 exactly:
- Low half (`addr<0x4000`): L2 enabled iff NOT MF-active, regardless of seg.
- High half (`addr in [0x4000, 0xC000)`): L2 enabled iff seg="11".
- `addr >= 0xC000`: L2 always disabled.
- `offset_pre = cpu_a(15:14)` when seg="11" else `seg`.

And the `layer2_active_page` gate `(sum & 0x70) == 0x70` matches VHDL
:2971 + :3101-3102 (sram_active = NOT layer2_A21_A13(8); the gate fires
when sum(6:4)="111" → page(7:5) = "111"). Reads return 0xFF, writes
dropped. Verify10 fix.

### 9. Sprite base address NR 0x15/0x16

Reviewed for completeness — these are sprite-related, not memory-subsystem
state. Out of scope.

### 10. MMU+1 wraparound at slot 7 → slot 0

Verified. `Mmu::read()` / `write()` use `addr >> 13` to compute slot
(0..7), never indexing past slot 7. The contention table boundary
(`slot_contended_[]` is size 4, indexed by `addr >> 14`) handles
0x0000/0x4000/0x8000/0xC000 as 16K slots. No wraparound issue.

### 11. EFF7(3) RAM-at-0x0000 + machine_type interaction

Verified per Verify4: EFF7(3)=1 forces MMU0/1 to 0x00/0x01 on next paging
trigger fire (port_memory_change_dly='1'). NR 0x50/0x51=0xFF does NOT
trigger this gate (NR 0x50/0x51 fires `nr_mmu_we` not
`port_memory_change_dly`), so explicit NR $50,$FF / $51,$FF writes leave
the slot serving legacy ROM (sram_rom-derived) regardless of EFF7(3).
This is `Verify4-memory class-(a)` fix — confirmed correct.

### 12. +3 special paging table

Verified the C++ `apply_plus3_special_paging_()` table:
- `(B=0, A=0)`: banks {0,1,2,3} → MMU{0/1, 2/3, 4/5, 6/7} = {0/1, 2/3, 4/5, 6/7}
- `(B=0, A=1)`: banks {4,5,6,7} → MMU = {8/9, 10/11, 12/13, 14/15}
- `(B=1, A=0)`: banks {4,5,6,3} → MMU = {8/9, 10/11, 12/13, 6/7}
- `(B=1, A=1)`: banks {4,7,6,3} → MMU = {8/9, 14/15, 12/13, 6/7}

Bit-reconstruction against VHDL :4625-4632 confirms all four
configurations exactly. Exit-special transition correctly reverts MMU2/3
to 0x0A/0x0B and MMU4/5 to 0x04/0x05 (per VHDL :4653-4658, :4665-4670).

### 13. NR 0x69 multi-fan-out on a single write

NR 0x69 fans into FOUR distinct sites per VHDL :3617-3625, :3658-3660,
:3924-3925:
- `port_ff_reg(5:0)` (Timex screen mode bits — VHDL :3617-3620)
- `port_7ffd_reg(3)` (ULA shadow display — VHDL :3658-3660)
- `port_123b_layer2_en` (V13-MEM-01 — VHDL :3924-3925)
- `Layer2::enabled_` (renderer side)

The C++ NR 0x69 handler at emulator.cpp:2210 fans into all four:
- `layer2_.set_enabled(...)` (Layer2's enable for renderer)
- `mmu_.set_l2_enable(...)` (V13-MEM-01 fix)
- `mmu_.set_port_7ffd_bit3(...)` + `renderer_.ula().set_shadow_screen_en(...)` (shadow fan-out)
- `port_ff_reg_ = (port_ff_reg_ & 0xC0) | (v & 0x3F)` + `renderer_.ula().set_screen_mode(...)` (port_ff fan-out)

All four propagation chains verified. ✓

### 14. NR 0x8E lock bypass + bit-3=0 MMU6/7 suppression

Verified per VHDL :3662-3672 / :3814 / :4677:
- NR 0x8E always fires (no port_7ffd_locked gate per VHDL :3662 elsif chain).
- `port_memory_change_dly` fires on `nr_8e_we`, so MMU0..7 rebuild — UNLESS bit 3=0, in which case `port_memory_ram_change_dly = NOT (nr_8e_we AND NOT bit3) = 0`, so MMU6/7 are NOT rebuilt.

C++ `write_nr_8e` correctly:
1. Updates port_7ffd_/1ffd_/dffd_ registers per VHDL :3658-3734.
2. Calls `apply_paging_update_()` (full rebuild) if bit 3=1, OR if special-mode entry/exit.
3. Otherwise calls `apply_legacy_rom_slots_()` (MMU0/1 only — MMU6/7 untouched).

Matches VHDL exactly. ✓

## Tests / build status

- Build: Release mode, full target — **success**.
- ctest: 38/38 PASS.
- FUSE Z80: 1356/1356 PASS.
- Pre-existing tests: zero regressions, no changes made.

## Conclusion

After 13 prior passes (~96 class-(a) fixes + 22 class-(b)/(c) resolved
+ 105 new regression tests across the 4 boot-critical subsystems), the
memory subsystem of jnext is at the convergence honestly claimable:
**no further class-(a/b/c) finding could be surfaced under the
exhaustive-mirror-check, port-read-back-consistency,
save/load-round-trip, multi-overlay-precedence, NR-03-transition, and
NR 0x69 multi-fan-out angles enumerated above**. Remaining open items
are all class-(d) (architectural) and listed in the prior passes'
aggregate report.

This Pass-14 audit is **claiming convergence** for the memory
subsystem.
