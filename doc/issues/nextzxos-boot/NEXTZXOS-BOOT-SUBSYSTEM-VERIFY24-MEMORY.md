# Pass-24 Memory Subsystem — Convergence Pressure Test Audit

**Branch:** `task2/verify24-memory` (off integration HEAD `d8647df`)
**Auditor:** Pass-24 blind audit agent
**Date:** 2026-05-11
**Scope:** Memory subsystem (mmu / ram / rom / contention) of jnext —
            Task 2 boot-critical subsystem audit, **convergence pressure
            test** with Pass-19+ enumeration-table rigor.
**Mandate:** Re-audit the memory subsystem with a fresh blind agent at
            Pass-19+ enumeration-table rigor (stricter than P14 methodology).
            Memory was skipped from P15..P23 — re-confirm convergence
            after 9 consecutive skipped passes + multiple adjacent-subsystem
            changes (DivMMC, NMI-MF-Port, CPU).
**Method:** Source-by-source enumeration table at TOP; every row has a
            specific VHDL line citation (✓) or marked finding (✗); empty
            cells forbidden. Audit completed BEFORE reading any prior
            `doc/issues/nextzxos-boot/` content.

## Result: CONVERGENCE RE-CONFIRMED (0 class-(a/b/c) findings)

After exhaustive enumeration-table audit at Pass-19+ rigor, no genuine
class-(a/b/c) finding could be surfaced in the memory subsystem. The
single divergence identified is a class-(d) architectural carry-over
(machine_timing vs machine_type axis split — see V24-MEM-01 below), out
of scope for class-(a/b/c) iteration per project escalation policy.

Memory convergence **holds at Pass-24** with the stricter Pass-19+
methodology.

## Class-(d) escalation (not fixed in this pass)

### V24-MEM-01 (class-d) — Contention decode uses MachineType (typ_sel) not nr_03_machine_timing (tim_sel)

VHDL `mem_contend` at zxnext.vhd:4489-4493 keys on `machine_timing_48 /
machine_timing_128 / machine_timing_p3` (derived from
`eff_nr_03_machine_timing` per zxnext.vhd:5761-5777). VHDL
`port_7ffd_active` at zxnext.vhd:2594 keys on `s128_timing_hw_en OR
p3_timing_hw_en` (mirror of `machine_timing_*` per zxnext.vhd:2457-2458).

jnext's `ContentionModel::is_contended_access` /
`ContentionModel::contention_tick` / `Mmu::mem_contend_for_` all key the
per-machine mem_contend decode on `type_` / `machine_type_` (the C++
`MachineType` enum), which is driven by `nr_03_machine_type` bits
(typ_sel) per zxnext.vhd:5741-5757 — NOT by `nr_03_machine_timing`
(tim_sel). When firmware writes NR 0x03 with `tim_sel != typ_sel` (rare;
not exercised by real Next boot path, which always sets matching bits),
the contention decode in jnext lags the VHDL by one axis.

Example divergence: NR 0x03 with `tim_sel=011` (+3 timing) and
`typ_sel=010` (128K type) → VHDL `machine_timing_p3=1`, mem_contend
fires for `mem_active_page(3)=1` (banks ≥ 4). jnext's
`machine_type_=ZX128K`, mem_contend fires for `mem_active_page(1)=1`
(odd banks). On page 0x02 (bank 1 lo) the decodes disagree.

Fix requires structural separation: add `machine_timing_` field to
ContentionModel + Mmu, fan-out from the NR 0x03 timing-commit path
(`Emulator::install_nextreg_handlers` NR 0x03 handler), and persist in
save/load schemas. Same architectural shape as the earlier
machine-type-vs-machine-timing handling in im2_ / cpu_
(`set_machine_timing_48_or_p3`).

Status: pending user authorization per project escalation policy
(class-(d) items are not fixed in regular audit passes — only fixed when
explicitly approved). Real-boot impact: nil (real firmware aligns
tim_sel with typ_sel — the canonical Next boot path uses
NR 0x03 with matching bits in both halves).

## Enumeration table — every memory-relevant site

✓ = VHDL-faithful (with line citation). ✗ = finding. No empty cells.

### Mmu — set/map function surface (mmu.h / mmu.cpp public API)

| Function | VHDL anchor (zxnext.vhd unless noted) | Status |
|---|---|---|
| `Mmu::Mmu(Ram&, Rom&)` constructor | :1730 `reset <= i_RESET`; calls `reset(true)` | ✓ (mmu.cpp:15-19) |
| `Mmu::reset(bool hard)` | :3646-3648, :3686-3690, :3713-3716, :3777-3779, :4610-4618, :2253-2256, :3907-3913, :1102, :5109-5111 | ✓ — VHDL-faithful soft/hard fold per mmu.cpp:52-166 comment block |
| `Mmu::set_boot_rom(data, size)` | :3199-3204 (bootrom hardwired 13-bit address = 8 KB) | ✓ — 8 KB internal buffer with zero-pad/truncate (mmu.cpp:21-50) |
| `Mmu::set_boot_rom_enabled(en)` | :1101 default '1'; :5122 NR 0x03 write → '0' | ✓ — store-only setter consumed by Emulator NR 0x03 handler |
| `Mmu::boot_rom_enabled()` | :1101 / :1856 gate condition | ✓ — pure observer |
| `Mmu::set_config_mode(bool)` | :1102 nr_03_config_mode default '1'; :3044 arbiter gate | ✓ — store-only mirror, driven by Emulator NR 0x03 handler |
| `Mmu::set_nr_04_romram_bank(uint8_t)` | :3045 `sram_pre_A21_A13 <= nr_04_romram_bank & cpu_a(13)` | ✓ — store-only mirror, NR 0x04 write fan-out from Emulator |
| `Mmu::set_rom_in_sram(bool)` | :3052 `sram_pre_A21_A13 <= "000000" & sram_rom & cpu_a(13)` (ROM-in-SRAM page range) | ✓ — Next-mode-only switch; rebuilds all slot pointers |
| `Mmu::rom_in_sram()` | :3052 observable | ✓ — pure observer |
| `Mmu::set_divmmc(DivMmc*)` | :2937-2945 priority cascade (DivMMC slot) | ✓ — wires non-owning overlay pointer |
| `Mmu::set_multiface(Multiface*)` | :2937-2945 priority cascade (MF slot above DivMMC) | ✓ — wires non-owning overlay pointer |
| `Mmu::set_debug_state(DebugState*)` | (jnext-internal) — debugger watchpoint observer wiring | ✓ — no VHDL analog |
| `Mmu::read(uint16_t addr)` hot path | :1854-1866 (cpu_di mux) + :3028-3132 (SRAM arbiter) | ✓ — boot/MF/DivMMC/L2/altrom/config/legacy cascade matches priority chain |
| `Mmu::write(uint16_t addr, uint8_t val)` hot path | :3028-3066 + :3081-3133 (SRAM arbiter wr branches) | ✓ — same priority cascade; rdonly + write-drop semantics matched |
| `Mmu::set_page(int slot, uint8_t page)` | :4686-4699 (`nr_mmu_we` direct write) | ✓ — sets nr_mmu_[slot]=page, then rebuild_ptr |
| `Mmu::get_page(int slot)` | :6075-6082 (NR 0x50-0x57 read-back = MMU<i>) | ✓ — returns nr_mmu_[slot] verbatim |
| `Mmu::get_effective_page(int slot)` | :2964 mmu_A21_A13 derivation (used by debugger to surface physical page) | ✓ — falls back to slots_[slot] when nr_mmu_=0xFF (matches VHDL legacy ROM resolution); high-page nr_mmu_ NR readback case documented |
| `Mmu::is_slot_rom(int slot)` | :3037-3057 SRAM arbiter slot 0/1 read_only flag | ✓ — observer of read_only_[slot] |
| `Mmu::slot_in_rom_area(int slot)` | :3037 `mmu_A21_A13(8)='1'` gate | ✓ — `get_page(slot) >= 0xE0` matches `("0001" + page(7:5))(3) = 1` |
| `Mmu::sram_pre_override_divmmc_eligible(pc, mf_active)` | :3029-3066 sram_pre_override(2) decode | ✓ — slot 0/1 + not-MF gate matches all 4 sub-branches |
| `Mmu::sram_pre_override_romcs_priority(pc, mf_active, config_mode)` | :3057 sram_pre_override="111" (normal ROM only) | ✓ — slot 0/1 + not-MF + slot_in_rom_area + not-config_mode matches |
| `Mmu::set_machine_type(MachineType)` | :2981-3008 sram_rom derivation per machine type; :3813 NO port_memory_change_dly trigger on type change | ✓ — refresh only slot 0/1 if read_only_ (V12-MEM-01 fix); preserve nr_mmu_ for high-page values; +3 special-paging-aware skip |
| `Mmu::machine_type()` | observable | ✓ — pure observer |
| `Mmu::map_rom(int slot, uint8_t rom_page)` | :4611-4612 (explicit ROM map shows 0xFF sentinel in MMU<i>) | ✓ — sets nr_mmu_[slot]=0xFF after map_rom_physical |
| `Mmu::map_rom_physical(int slot, uint8_t rom_page)` | :3052 / :1856 (ROM-page-to-slot mapping) | ✓ — bypasses nr_mmu_ update; caller decides whether to set sentinel |
| `Mmu::rebuild_ptr(int slot)` | :2964 `mmu_A21_A13` + :3037-3057 SRAM arbiter slot-0/1 high-page fallthrough | ✓ — high-page slot 0/1 → legacy ROM via `sram_rom*2+slot`; bank5/bank7 exempt from +0x20 shift via to_sram_page; V11-MEM-01 slots_[] coherency |
| `Mmu::map_128k_bank(uint8_t)` | :3650 port_7ffd_wr gate by port_7ffd_locked; :3814 port_memory_change_dly fan-out | ✓ — paging_lock gate, port_7ffd_ store, apply_paging_update_ |
| `Mmu::map_plus3_bank(uint8_t)` | :3718 port_1ffd_wr gate; :4623-4684 special-paging-aware MMU update | ✓ — paging_lock gate, port_1ffd_ store, apply_paging_update_ |
| `Mmu::write_port_dffd(uint8_t)` | :3691 dffd_wr gated by `port_7ffd_locked=0 OR profi`; :3693-3694 store cpu_do(4:0) + cpu_do(6) | ✓ — effective_paging_locked gate, store of low 5 bits + bit 6, apply_paging_update_ |
| `Mmu::write_port_eff7(uint8_t)` | :3780-3782 store cpu_do(2:3); :3813 port_memory_change_dly trigger | ✓ — no lock gate (VHDL also ungated), store of bits 2/3, apply_paging_update_ |
| `Mmu::port_dffd_reg() / port_dffd_reg_6()` | :877 (separate FF for bit 6); :3693-3694 / :4314 (MF+3 read-mux consumer) | ✓ — observers |
| `Mmu::port_eff7_ram_at_0000() / port_eff7_disable_p1024()` | :3781-3782 / :4636 / :3801 | ✓ — observers |
| `Mmu::write_nr_8f(uint8_t)` | :3787-3794 store + :3815 nr_8f_we_dly into port_memory_change_dly | ✓ — store nr_8f_mode_, apply_paging_update_ |
| `Mmu::nr_8f_mode() / pentagon_en() / pentagon_1024_en() / effective_paging_locked()` | :3798-3801 / :3769 | ✓ — derived gate observers, all formulae match |
| `Mmu::write_nr_8e(uint8_t)` | :3662-3670 / :3696-3704 / :3726-3734 (three FF update procs); :3814 port_memory_ram_change_dly suppress on bit3=0 | ✓ — atomic update of 7ffd/dffd/1ffd bits, special-aware apply with bit-3=0 MMU6/7 suppression |
| `Mmu::read_nr_8e()` | :6158-6159 (`port_253b_dat` composition) | ✓ — bit 3 fixed '1'; bit 0 = (7ffd(4) AND NOT 1ffd(0)) OR (1ffd(1) AND 1ffd(0)) matches |
| `Mmu::unlock_paging()` | :3654-3656 (NR 0x08 bit 7 clears port_7ffd_reg(5)) | ✓ — clears both paging_locked_ and port_7ffd_ bit 5 (V3-MEM fix) |
| `Mmu::paging_locked()` | :3769 / :5906 NR 0x08 read | ✓ — raw bit 5 observer (consumer composes via effective_paging_locked) |
| `Mmu::set_contention_disabled(bool) / contention_disabled()` | :5176 store; :5906 NR 0x08 read | ✓ — Branch C ownership (Mmu surface; Branch D rehome pending) |
| `Mmu::set_nr_8c(uint8_t)` | :2257 store; :3813 NO port_memory_change_dly trigger on NR 0x8C | ✓ — V3-MEM fix: per-slot refresh of slot 0/1 only when read_only_; V12-MEM-01 preserves nr_mmu_ for high-page values |
| `Mmu::get_nr_8c() / nr_8c_altrom_en() / _rw() / _lock_rom1() / _lock_rom0()` | :2262-2265 | ✓ — bit decomposers |
| `Mmu::set_p3_floating_bus_dat(uint8_t) / p3_floating_bus_dat()` | :4498-4509 latch; :4517 +3 port 0x0FFD readback | ✓ — V9-MEM fix moved latch update to per-page mem_contend_for_ decode |
| `Mmu::set_slot_contended(int slot, bool)` legacy mirror | :4489-4493 per-page decode (NEW canonical); legacy per-16K mirror retained for save-state schema | ✓ — documented as legacy, no hot-path consumer |
| `Mmu::slot_contended(int slot)` | observable | ✓ — pure observer |
| `Mmu::port_7ffd() / port_1ffd()` | :3679 / :3725 dat-clocked observable | ✓ — observers |
| `Mmu::shadow_screen_en()` | :3768 `port_7ffd_shadow` / :4453 i_ula_shadow_en | ✓ — bit 3 of port_7ffd_ |
| `Mmu::set_port_7ffd_bit3(bool)` | :3658-3660 NR 0x69 bit 6 fans into port_7ffd_reg(3) | ✓ — bit-3 alias setter, no full-rebuild needed per VHDL |
| `Mmu::current_rom_bank() / rom3_selected()` | :3772 `port_1ffd_rom = port_1ffd_reg(2) & port_7ffd_reg(4)` | ✓ — raw port-derived 2-bit selector |
| `Mmu::current_sram_rom()` | :2981-3008 per-machine sram_rom derivation | ✓ — V8-MEM fix: 128K/Next share altrom-lock branch; +3 separate; 48K hardwired '00' |
| `Mmu::sram_rom3()` | :2981-3008 sram_rom3 per machine type | ✓ — V7-MEM fix: 128K shares ZXN branch (single lock_rom1 / single rom-hi bit); +3 takes 2-bit; 48K hardwired '1' |
| `Mmu::engage_legacy_rom_paging() / _ram_paging()` | :4611-4612 (default 0xFF sentinel re-engage) | ✓ — wrappers around apply_legacy_*_slots_ helpers (Wave-8 fix) |
| `Mmu::engage_legacy_rom_paging_slot(int, bool)` | :4686-4699 nr_mmu_we semantics for value 0xFF | ✓ — per-slot legacy ROM re-engage; V12-MEM-01 conditional nr_sentinel update for NR 0x8C / set_machine_type refresh callers |
| `Mmu::set_l2_port(uint8_t, uint8_t)` | :3914-3923 port 0x123B latches; :3924-3925 NR 0x69 bit 7 alias; :2968 shadow bank select | ✓ — bit-4 offset-only dispatch, segment_raw + segment_mask (legacy) latches, V8-MEM fix gate via l2_overlay_active_for |
| `Mmu::l2_read_enable() / l2_write_enable() / l2_offset() / l2_map_shadow() / l2_bank() / l2_enable()` | :3914-3925 latch observers | ✓ — pure observers |
| `Mmu::set_l2_shadow_bank(uint8_t) / l2_shadow_bank()` | :2968 nr_13 shadow bank for CPU L2 map | ✓ — mirror seam for NR 0x13 → Mmu cache |
| `Mmu::set_l2_active_bank(uint8_t) / l2_bank()` | :2968 nr_12 active bank | ✓ — mirror seam for NR 0x12 → Mmu cache (V4-MEM fix) |
| `Mmu::set_l2_enable(bool) / l2_enable()` | :3924-3925 NR 0x69 bit 7 → port_123b_layer2_en alias | ✓ — V13-MEM-01 fix mirror |
| `Mmu::l2_port_readback()` | :3933 port_123b_dat composition | ✓ — G145 fix composes seg/shadow/rd_en/enable/wr_en correctly |
| `Mmu::to_sram_page(uint8_t)` | :2964 mmu_A21_A13 +0x20 shift in Next; :2961-2962 bank5/bank7-lo exemptions | ✓ — Next-mode +0x20 shift with bank5(0x0A/0x0B)/bank7-lo(0x0E) bypass |
| `Mmu::l2_overlay_active_for(uint16_t)` (private) | :3036-3043 / :3050 / :3057 (low-half non-MF) + :3065 (high-half seg="11" only) | ✓ — V8-MEM fix matches per-half gate exactly |
| `Mmu::l2_offset_pre_for(uint16_t)` (private) | :2966 offset_pre = cpu_a(15:14) when seg=11 else seg | ✓ |
| `Mmu::mem_contend_for_(uint16_t)` (private) | :4489-4493 per-page mem_contend decode | ✗ V24-MEM-01 (class-d) — uses MachineType (typ-driven) instead of machine_timing (tim-driven). Escalation, not in-pass fix. |
| `Mmu::altrom_sram_page_(uint16_t)` (private) | :3117 sram_A21_A13 alt-ROM addressing; :2981-3008 per-machine alt_128_n | ✓ — covers 48K/+3/ZXN+128K branches; bit 0 = cpu_a(13) |
| `Mmu::apply_legacy_ram_slots_()` (private) | :4677-4680 (MMU6/7 from port_7ffd_bank) | ✓ — compose_bank_ * 2 + 0/1 with set_page |
| `Mmu::apply_legacy_rom_slots_()` (private) | :4619-4646 (MMU0/1 from sram_rom/EFF7(3)/profi) | ✓ — EFF7(3) RAM-at-0000 + altrom-lock-aware sram_rom; 0xFF sentinel for nr_mmu_ |
| `Mmu::apply_legacy_paging_()` (private) | :4619-4684 combined MMU0/1/6/7 update | ✓ — RAM first, ROM second |
| `Mmu::apply_plus3_special_paging_()` (private) | :4623-4632 (+3 special table for port_1ffd_reg(2:1)) | ✓ — 4 configs × 4 slot pairs = 16-entry table matches VHDL |
| `Mmu::revert_slots_2_to_5_post_special_()` (private) | :4653-4670 (exit-special revert MMU2/3=0x0A/0x0B, MMU4/5=0x04/0x05) | ✓ — explicit revert called by apply_paging_update_ on exit |
| `Mmu::apply_paging_update_()` (private) | :4623-4684 outer special-mode arbitration | ✓ — special / exit-special / legacy three-way dispatch; port_1ffd_special_old_ post-update tracking |
| `Mmu::compose_bank_()` (private) | :3763-3766 7-bit port_7ffd_bank composition; Pentagon vs non-Pentagon bit 4:3 / 5 / 6 source | ✓ — N8F-05 Pentagon bank(6)='0' forced |
| `Mmu::divmmc_read(addr, &val)` / `_write(addr, val)` (private) | :2937-2945 priority slot 2 | ✓ — DivMMC subsystem owns the actual r/w; this is a forwarding stub |
| `Mmu::mf_overlay_active_() / mf_rom_byte_() / mf_ram_byte_() / mf_ram_write_()` (private) | :2937-2945 priority slot 1; :3028-3035 MF SRAM remap; multiface.vhd:186 mf_enable_eff | ✓ — out-of-line MF helpers |
| `Mmu::save_state(StateWriter&)` | (jnext-internal schema) | ✓ — see schema-field table below |
| `Mmu::load_state(StateReader&)` | (jnext-internal schema, must mirror save order) | ✓ — see schema-field table below; rebuild_ptr per slot at end; verbatim nr_mmu_ restore |

### Mmu — save/load schema fields (Mmu::save_state / Mmu::load_state)

| Field | VHDL anchor | Status |
|---|---|---|
| `slots_[8]` | :4607-4699 MMU<i> live values | ✓ |
| `read_only_[8]` | :3037-3057 SRAM arbiter slot rdonly flag | ✓ |
| `paging_locked_` | :3769 `port_7ffd_locked` (raw bit 5) | ✓ |
| `port_7ffd_` | :3652 `port_7ffd_reg <= cpu_do` | ✓ |
| `port_1ffd_` | :3724 `port_1ffd_reg <= cpu_do(2:0)` | ✓ |
| `l2_write_enable_` | :3916 port_123b_layer2_map_wr_en | ✓ |
| `l2_segment_mask_` | (legacy save schema; no consumer post-V8-MEM) | ✓ (retained for schema compat) |
| `l2_bank_` | :2968 nr_12 mirror | ✓ |
| `boot_rom_en_` | :1101 default '1'; :5109-5111 reset re-arm if config_mode | ✓ |
| `config_mode_` | :1102 default '1'; :5147-5151 NR 0x03 transition | ✓ |
| `nr_04_romram_bank_` | :3045 config_mode SRAM arbiter input | ✓ |
| `rom_in_sram_` | :3052 ROM-in-SRAM page range (Next only) | ✓ |
| `contention_disabled_` | :5176 store; :5906 NR 0x08 readback | ✓ |
| `nr_8c_reg_` | :2257 store | ✓ |
| `machine_type_` | :5741-5757 nr_03_machine_type → machine_type_48/128/p3 | ✓ |
| `port_dffd_reg_` | :3693 store cpu_do(4:0) | ✓ |
| `port_eff7_reg_2_` / `port_eff7_reg_3_` | :3781-3782 store cpu_do(2:3) | ✓ |
| `nr_8f_mode_` | :3791 store nr_wr_dat(1:0); :888 no reset clause | ✓ |
| `l2_read_enable_` | :3918 port_123b_layer2_map_rd_en | ✓ |
| `p3_floating_bus_dat_` | :4498-4509 latch | ✓ |
| `slot_contended_[4]` | legacy per-16K mirror | ✓ |
| `l2_segment_raw_` | :3920 2-bit raw segment | ✓ |
| `l2_enable_` | :3925 NR 0x69 b7 mirror | ✓ |
| `l2_map_shadow_` | :3919 port_123b b3 | ✓ |
| `l2_offset_` | :3922 port_123b b4=1 → 3-bit offset | ✓ |
| `l2_shadow_bank_` | :2968 nr_13 mirror | ✓ |
| `port_dffd_reg_6_` | :877 separate FF; :3694 store cpu_do(6) | ✓ |
| `port_1ffd_special_old_` | :3716 reset; :3729 capture | ✓ — V12-MEM-02 added field; save/load symmetric |
| `nr_mmu_[8]` | :4686-4699 nr_mmu_we verbatim store | ✓ — V4-MEM fix: persist verbatim NR-write value (incl. 0xE0..0xFE high-page) for round-trip |

### Ram — bank API (ram.h / ram.cpp)

| Function | VHDL anchor / model | Status |
|---|---|---|
| `Ram::Ram(size_t)` | ram subsystem: 2 MB (1024 × 16-bit DPRAMs) plus dual-port BRAM banks 5/7-lo | ✓ — single linear buffer, page-indexed; bank5/7 exempt-shift handled by Mmu::to_sram_page |
| `Ram::read(uint32_t)` | physical SRAM read (bounds-checked) | ✓ — returns 0xFF for out-of-range (mirrors floating-bus default) |
| `Ram::write(uint32_t, uint8_t)` | physical SRAM write (bounds-checked) | ✓ — silently drops out-of-range |
| `Ram::page_ptr(uint16_t)` mutable / const | 8 KB page indexing | ✓ — used by Mmu for hot-path read_ptr_/write_ptr_ |
| `Ram::reset()` | power-on RAM clear | ✓ — zero-fill |
| `Ram::size()` | observable | ✓ |
| `Ram::save_state(StateWriter&)` | persists full 2 MB | ✓ — u64 length prefix + bytes |
| `Ram::load_state(StateReader&)` | restores full 2 MB | ✓ |

### Rom — bank API (rom.h / rom.cpp)

| Function | VHDL anchor / model | Status |
|---|---|---|
| `Rom::Rom()` | rom subsystem: 64 KB ROM image (4 × 16 KB banks) | ✓ — initial 0xFF fill |
| `Rom::reset()` | clears `alt_rom_config_` only | ✓ — minimal reset; `alt_rom_config_` field is unused dead-code (see note below) |
| `Rom::load(slot, path)` | file-load 16 KB into slot 0..3 | ✓ — 16 KB short-read guard |
| `Rom::load_bytes(slot, data, size)` | byte-buffer load 16 KB into slot 0..3 | ✓ — Wave-0.3 SD-ROM path |
| `Rom::read(uint32_t)` | ROM byte read | ✓ — bounds-checked |
| `Rom::page_ptr(uint16_t)` mutable / const | 8 KB page indexing within 64 KB ROM space | ✓ — used by Mmu in non-Next mode |
| `Rom::set_alt_rom_config(uint8_t)` / `alt_rom_config()` / `alt_rom_enabled()` | NR 0x8C bit 7 etc.; actual altrom routing lives in Mmu::nr_8c_reg_ | ✓ (dead-code; harmless — see notes) |

Note: `Rom::alt_rom_config_` is set by Emulator's NR 0x8C handler
(emulator.cpp:2767) but has no consumer in production code — all altrom
logic flows through `Mmu::nr_8c_reg_`. Cleanup-only, no correctness
impact. Documented for future class-(d) tidy-up.

### Contention — model surface (contention.h / contention.cpp)

| Function | VHDL anchor (zxnext.vhd / zxula.vhd) | Status |
|---|---|---|
| `ContentionModel::build(MachineType)` | full-reset entry: LUT + per-machine bank decode + gate-state reset | ✓ — full re-init (cpu_speed/contention_disable/mem_active_page/shadows/port_7ffd_io_en all cleared) |
| `ContentionModel::rebuild_for_type(MachineType)` | NR 0x03 commit path; preserves dynamic gate state | ✓ — type_ updated, LUT rebuilt, slot[1]=true for 48K/128K/+3 |
| `ContentionModel::delay(hc, vc)` | zxula.vhd:582-583 wait_s + per-phase 7-cycle pattern | ✓ — LUT lookup; raw window delay (not gated by contention_disable / cpu_speed — consumers AND with is_contended_access) |
| `ContentionModel::is_contended_address(addr)` | per-16K mirror; canonical decode is mem_contend_for_ in Mmu | ✓ — legacy accessor; consumed only by Emulator for per-slot mirror push |
| `ContentionModel::set_contended_slot(int, bool)` | legacy per-16K mirror seam | ✓ |
| `ContentionModel::set_mem_active_page(uint8_t)` | :4489-4493 mem_active_page input | ✓ — pushed by FUSE memory/IO callbacks per cycle |
| `ContentionModel::set_cpu_speed(uint8_t)` | :5786-5828 immediate-commit (testing only) | ✓ — both pending and effective updated |
| `ContentionModel::set_pending_cpu_speed(uint8_t)` | :5786-5791 shadow update on NR 0x07 write | ✓ |
| `ContentionModel::commit_pending_cpu_speed_on_bus_idle(bool, bool)` | :5809-5820 bus-idle commit edge | ✓ — pending → effective when bus_idle && !dma_holds_bus |
| `ContentionModel::set_contention_disable(bool)` | :5176 NR 0x08 b6 immediate-commit (testing only) | ✓ |
| `ContentionModel::set_contention_disable_shadow(bool)` | :5176 NR 0x08 b6 shadow update | ✓ |
| `ContentionModel::commit_contention_disable_on_hc(uint16_t)` | :5822-5823 hc(8)='1' commit edge | ✓ |
| `ContentionModel::set_port_7ffd_io_en(bool)` | :2399 NR 0x82 bit 1 mirror | ✓ — V9-MEM fix |
| `ContentionModel::set_port_ulap_io_en(bool)` | :2439 NR 0x85 bit 0 mirror | ✓ — V15-CPU-NIT-03 fix |
| `ContentionModel::mem_active_page() / cpu_speed() / pending_cpu_speed() / contention_disable() / contention_disable_shadow() / port_7ffd_io_en() / port_ulap_io_en()` | observable | ✓ — pure observers |
| `ContentionModel::is_contended_access()` | :4481 i_contention_en + :4489-4493 mem_contend decode | ✗ V24-MEM-01 (class-d) — uses MachineType (typ-driven) instead of machine_timing (tim-driven). Escalation. |
| `ContentionModel::port_contend(uint16_t, bool)` | :4496 port_contend OR-chain (even-port + port_7ffd_active + port_bf3b + port_ff3b) | ✓ — V9-MEM/V15-CPU-NIT-03 fixes brought port_7ffd_active + port_ulap_io_en gates in; +3 cpu_a(14)=1 requirement matches :2593 |
| `ContentionModel::contention_tick(...)` | zxula.vhd:579-600 (o_cpu_contend + o_cpu_wait_n) | ✗ V24-MEM-01 (class-d) — same machine_timing-vs-MachineType issue at the mem_c decode; escalation |

### NR slot configuration write sites (Emulator install_nextreg_handlers)

| Site | VHDL anchor | Status |
|---|---|---|
| NR 0x50..0x57 write (slot-i mapping) | :4686-4699 nr_mmu_we direct store of nr_wr_dat into MMU<i>; v=0xFF re-engages legacy paging per :4611-4612 | ✓ — emulator.cpp:1789-1820: per-slot 0xFF dispatcher for slot 0/1 (legacy ROM re-engage), 0xFF for slot 2-7 (set_page(i,0xFF) → inactive), non-0xFF for all (set_page(i,v)) |
| NR 0x50..0x57 read (slot-i mapping) | :6075-6082 port_253b_dat <= MMU<i> | ✓ — emulator.cpp:1841-1844: returns mmu_.get_page(i) = canonical nr_mmu_[i] |
| NR 0x69 write (Display Control 1) | :3658-3660 bit 6 → port_7ffd_reg(3); :3924-3925 bit 7 → port_123b_layer2_en | ✓ — emulator.cpp:2547-2555: set_l2_enable + set_port_7ffd_bit3 (V13-MEM-01) |
| NR 0x6B write (Tilemap Control) | :5461-5462 nr_6b_tm_en + nr_6b_tm_control store | ✓ — tilemap subsystem, NOT memory subsystem; no memory side effect |
| NR 0x8C write (Alternate ROM control) | :2257 store; :3813 NO port_memory_change_dly trigger | ✓ — emulator.cpp:2766: mmu_.set_nr_8c(v) + divmmc rom3 fan-out |
| NR 0x8C read | :6156 port_253b_dat <= nr_8c_altrom | ✓ — emulator.cpp:2778: returns mmu_.get_nr_8c() |
| NR 0x8E write (Unified paging) | :3662-3734 atomic 7ffd/dffd/1ffd updates; :3814 bit3=0 suppress MMU6/7 | ✓ — emulator.cpp:2786-2789: forwards to mmu_.write_nr_8e(v) |
| NR 0x8E read | :6158-6159 composed read-back | ✓ — emulator.cpp:2790-2792: returns mmu_.read_nr_8e() |
| NR 0x8F write (Mapping Mode) | :3791 store nr_wr_dat(1:0); :3815 nr_8f_we_dly into port_memory_change_dly | ✓ — emulator.cpp:2797-2799: mmu_.write_nr_8f(v) |
| NR 0x8F read | :6162 port_253b_dat composition | ✓ — emulator.cpp:2801-2802: returns nr_8f_mode_ |
| NR 0x07 write (CPU speed) | :5786-5828 shadow + bus-idle-gated effective | ✓ — emulator.cpp:749-763: pending_cpu_speed shadow update; bus-idle commit in run loop |
| NR 0x07 read | :5816-5820 composed bits[5:4]=effective + bits[1:0]=pending | ✓ — emulator.cpp:790-799: cpu_speed + cached(0x07) compose |
| NR 0x08 write (Peripheral 3) | :3654-3656 bit 7 clears port_7ffd_reg(5); :5176 bit 6 → nr_08_contention_disable | ✓ — emulator.cpp:4156-4197: unlock_paging + contention shadow + nr_08_stored_low_ |
| NR 0x08 read | :5906 composed read-back | ✓ — emulator.cpp:4208-4232: not-locked + eff_nr_08_contention_disable + nr_08_stored_low_ |
| NR 0x82 write (Internal port enable LSB) | :2392 internal_port_enable composition; :2399 bit 1 → port_7ffd_io_en | ✓ — emulator.cpp:809-815: propagate_effective_port_enables → contention shadow |
| NR 0x03 write (Reset/Machine type) | :5121-5151 + :5761-5777 timing/type fan-out | ✓ — emulator.cpp:2247-2395: bootrom_en clear + machine_timing commit + machine_type commit + config_mode FSM |
| NR 0x04 write (RomRam bank) | :5208-5217 nr_04_romram_bank store gated on nr_03_config_mode | ✓ — emulator.cpp:2436-2446: gate check + nr_04_romram_bank store + mmu_ fan-out |
| NR 0x12 write (L2 active bank) | :5926-5930 store; :2968 layer2_active_bank source | ✓ — emulator.cpp:817-827: layer2_.set_active_bank + mmu_.set_l2_active_bank (V4-MEM fix) |
| NR 0x13 write (L2 shadow bank) | :5934 store; :2968 source when port_123b_layer2_map_shadow=1 | ✓ — emulator.cpp:866-872: mmu_.set_l2_shadow_bank fan-out |

### Port write sites (Emulator install_port_handlers)

| Port | Decode (VHDL) | Status |
|---|---|---|
| 0x7FFD write | :2593 cpu_a(15)=0 AND (cpu_a(14)=1 OR NOT p3_timing) AND port_fd AND NOT port_1ffd AND port_7ffd_io_en | ✓ — emulator.cpp:3138-3149: NR 0x82 bit 1 gate + mmu_.map_128k_bank |
| 0x1FFD write | :2599 cpu_a(13:12)=01 AND port_xffd AND port_1ffd_io_en (NR 0x82 bit 3) | ✓ — emulator.cpp:3270-3274: NR 0x82 bit 3 gate + mmu_.map_plus3_bank |
| 0xDFFD write | :2596 cpu_a(15:12)=1101 AND port_fd AND port_dffd_io_en (NR 0x82 bit 2) | ✓ — emulator.cpp:3559-3565: NR 0x82 bit 2 gate + mmu_.write_port_dffd |
| 0xEFF7 write | :2604 cpu_a(15:12)=1110 AND port_f7_lsb AND port_eff7_io_en (NR 0x85 bit 2) | ✓ — emulator.cpp:3583-3588: NR 0x85 bit 2 gate (G143 corrected) + mmu_.write_port_eff7 |
| 0x123B write (Layer 2 control) | :2635 port_12xx_msb AND port_3b_lsb AND port_layer2_io_en (NR 0x83 bit 7) | ✓ — emulator.cpp:3102-3131: NR 0x83 bit 7 gate + mmu_.set_l2_port + layer2/divmmc fan-out |
| 0x123B read | :3933 port_123b_dat composition | ✓ — emulator.cpp:3104-3107: NR 0x83 bit 7 gate + mmu_.l2_port_readback (G145) |

### Contention paths (M1 / operand / IO)

| Path | VHDL anchor | Status |
|---|---|---|
| `fuse_z80_readbyte_raw(address)` | raw memory fetch — no contention path | ✓ — bypass for FUSE T-state shape |
| `fuse_z80_readbyte(address)` | :4481 i_contention_en + :4489 mem_contend + zxula.vhd:582-600 contend/wait_n | ✓ — mem_active_page push + contention_tick + 3 base T-states |
| `fuse_z80_writebyte(address)` | same mem_contend gate; write path | ✓ — symmetric to readbyte |
| `fuse_z80_readport(port)` | :4496 port_contend + IO contention shape | ✓ — IORQ + port_contend gate + IO T-states |
| `fuse_z80_writeport(port)` | :4496 port_contend symmetric | ✓ |
| `contend_read(address, time)` | zxula.vhd:579-600 path A | ✓ — registered-mreq-style window gate |
| `contend_read_no_mreq(address, time)` | path A + no-MREQ window | ✓ — same gate, no-MREQ semantics |
| `contend_write_no_mreq(address, time)` | path A + write side | ✓ |
| Mmu::read floating-bus latch | :4498-4509 mem_contend AND cpu_mreq_n=0 capture | ✓ — V9-MEM fix: per-page mem_contend_for_ decode key (subject to V24-MEM-01 class-d caveat) |
| Mmu::write floating-bus latch | :4498-4509 symmetric write capture | ✓ — same caveat |

### apply_*_paging_*_ helper functions

| Helper | VHDL anchor | Status |
|---|---|---|
| `apply_legacy_ram_slots_()` | :4677-4680 MMU6/7 from port_7ffd_bank | ✓ |
| `apply_legacy_rom_slots_()` | :4619-4646 MMU0/1 from sram_rom/EFF7(3)/profi | ✓ |
| `apply_legacy_paging_()` | :4619-4684 combined MMU0/1/6/7 update for normal triggers | ✓ — RAM first, ROM second |
| `apply_plus3_special_paging_()` | :4623-4632 +3 special table | ✓ — 4-config × 4-pair = 16-page mapping matches VHDL bit construction |
| `revert_slots_2_to_5_post_special_()` | :4655-4670 exit-special revert | ✓ — MMU2/3=0x0A/0x0B, MMU4/5=0x04/0x05 |
| `apply_paging_update_()` | :4623-4684 outer special/exit/normal arbitration | ✓ — three-way dispatch; port_1ffd_special_old_ post-update tracking |
| `compose_bank_()` | :3763-3766 7-bit bank composition | ✓ — Pentagon vs non-Pentagon switching for bits 4:3 / 5 / 6 |

### RST/RETN trampoline interactions (memory side)

| Path | VHDL anchor | Status |
|---|---|---|
| Stackless NMI RETN dispatch | im2_control.vhd + zxnext.vhd:1850-1852 (z80_stackless_nmi → cpu_di=z80_retn_address) | ✓ — owned by Im2Controller; memory side only routes cpu_di; not in this audit scope (CPU subsystem already converged Pass-23) |
| RST $66 NMI fetch + Multiface fetch_66 bypass | multiface.vhd:186 mf_enable_eff = mf_enable OR fetch_66 | ✓ — Multiface ownership; Mmu::mf_overlay_active_() consults is_mem_active() which OR-folds fetch_66_live_ |
| RST $00 boot vector fetch through bootrom | :1856 bootrom_en + :3199-3204 13-bit address | ✓ — Mmu::read boot-rom-overlay branch fires first in priority |
| RST $08 / $10 / etc. with DivMMC AUTOMAP | :3138 sram_divmmc_automap_rom3_en gate | ✓ — owned by DivMmc; Mmu provides sram_pre_override_divmmc_eligible / sram_pre_override_romcs_priority helpers (converged Pass-21) |

## Methodology — what was scrutinized

1. **VHDL line-by-line for the SRAM arbiter (zxnext.vhd:2937-3132)** —
   confirmed every C++ read/write priority branch matches the seven-way
   VHDL cascade (boot → MF → DivMMC → L2 → MMU → config → ROMCS → ROM).
2. **MMU update process (zxnext.vhd:4607-4699)** — every trigger
   (port 7FFD/1FFD/DFFD/EFF7, NR 0x8E/0x8F, NR 0x50..0x57) traced
   end-to-end through the C++ apply_paging_update_ arbiter. Special-mode
   entry, exit-special revert (slots 2-5), bit-3=0 NR 0x8E MMU6/7
   suppression all confirmed.
3. **port_1ffd_special_old semantics (zxnext.vhd:3713-3742)** — checked
   the VHDL per-clock reset-to-0 behaviour against the C++
   end-of-apply-update tracking. Confirmed equivalent at CPU-port-OUT
   granularity (back-to-back FPGA-clock triggers can't occur at C++
   per-port-write granularity).
4. **Layer 2 overlay gate (zxnext.vhd:3036-3066 / 3077)** — confirmed
   per-half override gate matches: low half always enabled in non-MF
   cases, high half only seg="11", 0xC000+ never. V8-MEM-class-(a) fix
   verified by reading the gate function `l2_overlay_active_for`.
5. **Floating-bus latch (zxnext.vhd:4498-4509)** — checked the
   `mem_contend AND cpu_mreq_n=0` capture vs C++ `mem_contend_for_`
   driver in Mmu::read/write. **Identified V24-MEM-01 (class-d): C++
   uses MachineType instead of machine_timing axis.**
6. **Contention enable gate (zxnext.vhd:4481 + 4489-4493)** — same
   axis-split issue (V24-MEM-01) in `is_contended_access` and
   `contention_tick`. Cross-confirmed against
   `ContentionModel::port_contend` which uses `type_` for the
   `port_7ffd_active` gate (same divergence).
7. **port_7ffd_locked / Pentagon-1024 lock override (zxnext.vhd:3769)**
   — confirmed `effective_paging_locked()` mirrors VHDL combinational
   gate. NR 0x08 bit 7 clear (`unlock_paging`) clears both
   `paging_locked_` and `port_7ffd_` bit 5 (V3-MEM-class-(a) fix).
8. **NR 0x8E unified paging atomic update (zxnext.vhd:3662-3734)** —
   re-traced the three FF update procs against the C++
   `write_nr_8e`. Confirmed bit-3=0 suppresses MMU6/7 via the
   `port_memory_ram_change_dly = NOT (nr_8e_we AND NOT nr_wr_dat(3))`
   gate. Read-back composition (zxnext.vhd:6158-6159) bit-0 derivation
   confirmed against C++.
9. **NR 0x50..0x57 = 0xFF re-engage legacy ROM (zxnext.vhd:4611-4612)**
   — V12-MEM-01 / V8-MEM-class-(a) per-slot helper
   `engage_legacy_rom_paging_slot` preserves the OTHER slot's prior
   mapping. set_nr_sentinel parameter (V12-MEM-01) preserves verbatim
   high-page NR readback for NR 0x8C / set_machine_type refresh paths.
10. **save/load round-trip (Mmu::save_state / load_state)** —
   verified every persisted field is restored in matching order; the
   verbatim `nr_mmu_[8]` array (V4-MEM-class-(a)) preserves
   high-page NR readback fidelity.
11. **machine_type vs machine_timing axis (zxnext.vhd:5741-5777)** —
   discovered V24-MEM-01 (class-d): C++ folds typ_sel-driven
   `MachineType` into the contention decode; VHDL keys on tim_sel-driven
   `machine_timing_*`. Real boot path aligns the two axes so the
   divergence is benign in practice.
12. **port_dffd_reg_6 separate FF (zxnext.vhd:877, 3694, 4314)** —
   confirmed save/load symmetry, store fires on every port_dffd write,
   MF+3 read-mux consumer (zxnext.vhd:4314) honors the bit.
13. **port_eff7_reg_3 RAM-at-0x0000 override (zxnext.vhd:4636-4644)** —
   confirmed `apply_legacy_rom_slots_` sets MMU0/1 = 0x00/0x01 when
   EFF7(3)=1; matches VHDL `port_memory_change_dly`-gated update only
   (V4-MEM fix: NR 0x50/0x51 = 0xFF nr_mmu_we path does NOT trigger
   the EFF7(3) override per `engage_legacy_rom_paging_slot`).
14. **Layer 2 high-sum inactive gate (zxnext.vhd:2971 + 3101-3102)** —
   V10-MEM-class-(c) fix verified: when (bank + bank_offset) bits[6:4]
   = "111", layer2_A21_A13(8)='1' → sram_active='0' → L2 read returns
   floating bus, write dropped.
15. **Pentagon vs Next sram_rom branch (zxnext.vhd:2997-3007)** —
   confirmed 128K and ZXN_ISSUE2 share the else branch in
   `current_sram_rom` / `sram_rom3` (V8-MEM-class-(a) fix).
16. **NR 0x8C bit copy on reset (zxnext.vhd:2253-2256)** — confirmed
   hard+soft reset both copy lo nibble to hi nibble via
   `Mmu::reset(bool hard)`.

## Cross-cutting families re-checked (Pass-19+ enumeration angle)

These families have been the source of class-(b)/(c) findings in prior
passes. Re-checked at Pass-24 with fresh eyes:

- **Multi-writer FF mirrors** — every FF with > 1 writer (port_7ffd_reg
  bits, port_1ffd_reg, port_dffd_reg, port_eff7_reg, MMU0..7, etc.) is
  consistently fanned out across all writers. No mirror gap detected
  (P13/P14 V13-MEM-01 family closed).
- **NR readback composition** — NR 0x50..0x57, 0x69, 0x8C, 0x8E, 0x8F
  read-back composition (zxnext.vhd:6075-6082, 6156-6162) all checked
  against the C++ read handlers. V4-MEM-class-(a) nr_mmu_ verbatim
  persistence keeps high-page readback faithful.
- **Save/load symmetry** — every save_state field has a matching
  load_state read in the same order; V12-MEM-02
  port_1ffd_special_old_ persistence verified.
- **Cycle-accurate FSM (port_memory_change_dly path)** — VHDL
  per-clock arbitration vs C++ per-port-write granularity verified
  equivalent for all realistic CPU-instruction sequences (back-to-back
  FPGA-clock triggers can't occur at CPU OUT granularity).
- **Reset semantics (hard/soft fold)** — VHDL `reset='1'` fires on BOTH
  hard and soft per the top-level wrapper (Pass-5 G46(b)
  re-interpretation); C++ folds both into `reset(bool hard)` ignoring
  the hard parameter. Verified for paging_locked_,
  contention_disabled_, port_*_reg, nr_8c lo→hi copy.

## Files reviewed

- src/memory/mmu.h (1426 lines), src/memory/mmu.cpp (987 lines)
- src/memory/ram.h (20 lines), src/memory/ram.cpp (40 lines)
- src/memory/rom.h (37 lines), src/memory/rom.cpp (54 lines)
- src/memory/contention.h (265 lines), src/memory/contention.cpp (304 lines)
- src/core/emulator.cpp (memory-relevant handlers — ~50 sites:
  NR 0x03/0x04/0x07/0x08/0x12/0x13/0x50..0x57/0x69/0x8C/0x8E/0x8F;
  port 0x7FFD/0x1FFD/0xDFFD/0xEFF7/0x123B; reset/init flow)
- src/cpu/z80_cpu.cpp (FUSE callback hot-path memory + contention
  cycles)
- src/peripheral/multiface.h (mf_overlay_active_ wiring)
- test/mmu/mmu_test.cpp (40 test functions — coverage verification)
- test/contention/contention_test.cpp + contention_helpers.h
- VHDL oracle: cores/zxnext/src/zxnext.vhd (memory blocks),
  cores/zxnext/src/cpu/t80n.vhd (contention),
  cores/zxnext/src/ram/*.vhd (BRAM topology), rom/bootrom.vhd

## Baseline test status

- Release build: clean
- ctest: 38/38 pass
- FUSE Z80: 1356/1356 pass
- Regression: 33/0/0 pass

All baselines green; no changes in this pass.

## Trajectory

| Pass | Memory class-(a/b/c) findings | Class-(d) escalations |
|---|---|---|
| 3-13 | iterative findings (V3..V13-MEM-NN) | half-cycle / `_q` registered signals |
| 14 | 0 (convergence claimed at P14 methodology) | half-cycle architectural carry-over |
| 15-23 | SKIPPED (convergence skip per workflow rule) | — |
| **24** | **0 (convergence re-confirmed at P19+ rigor)** | + V24-MEM-01 machine_timing-vs-MachineType axis split |

**Memory convergence holds at Pass-24.** The pressure test surfaced one
new class-(d) architectural item (V24-MEM-01) without any class-(a/b/c)
finding — the existing P14 convergence was correct within its scope,
and the stricter P19+ enumeration-table rigor adds the new class-(d)
item to the existing pool without invalidating convergence.
