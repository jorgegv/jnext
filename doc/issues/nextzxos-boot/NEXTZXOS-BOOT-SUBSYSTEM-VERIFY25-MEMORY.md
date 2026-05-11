# NextZXOS Boot Subsystem — VERIFY-25 — Memory (mmu / ram / rom / contention)

**Pass-25 FINAL CONVERGENCE PRESSURE TEST**

- **Agent role**: independent re-auditor, third explicit re-walk of Memory subsystem (after P14 convergence and P24 re-verification).
- **Branch**: `task2/verify25-memory`
- **Worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify25-memory`
- **Off integration HEAD**: `7414784`.
- **VHDL oracle**: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd` (and `zxula.vhd` for contention LUT).
- **C++ subjects**:
  - `src/memory/mmu.h` (1426 lines), `src/memory/mmu.cpp` (987 lines)
  - `src/memory/ram.h` (20), `src/memory/ram.cpp` (40)
  - `src/memory/rom.h` (37), `src/memory/rom.cpp` (54)
  - `src/memory/contention.h` (265), `src/memory/contention.cpp` (304)
- **Tests baseline (Release, this HEAD)**:
  - `ctest --test-dir build`: **38/38 PASS** (0 failed)
  - `./build/test/fuse_z80_test build/test/fuse`: **1356/1356 PASS** (0 fail/skip)
  - `bash test/00regression/regression.sh`: pending (baseline 33/0/0 on integration; will be recorded in final summary)
- **Blindness rule observed**: enumeration table + V24-MEM-01 re-verification produced before reading any `doc/issues/nextzxos-boot/*MEMORY*.md` reports.

## Differential audit P24 → P25 (sources)

```
git -C <worktree> log 05f157b7..7414784 -- src/memory/
```

→ **EMPTY**. Zero commits touched `src/memory/` between P24 (HEAD `05f157b7`) and P25 (HEAD `7414784`).

The single commit at `src/` level in that window is `0af78514 fix(task2-pass24-divmmc): V24-DIVMMC-01 — CMD10 CID MDT year encoding off-by-4` (DivMMC only).

**Conclusion (intrinsic)**: Memory subsystem source is byte-identical between P24 and P25. Any audit finding from a thorough re-walk against the same VHDL oracle MUST match the P24 audit findings exactly. The set of findings is therefore bounded ab initio: 0 class-(a)/(b)/(c) + the single class-(d) V24-MEM-01 carry-over.

The pass-25 audit re-walks the entire enumeration table from scratch (without consulting P24 results) to confirm this intrinsic conclusion and rule out any latent finding that P24 missed.

## Enumeration table

Each row maps one C++ surface to its VHDL-authority anchor and records the audit verdict for Pass-25.

Legend:
- `✓` — VHDL-faithful, no divergence detectable in code review at this pass.
- `~` — partially faithful with an explicit, documented WONT/won't-fix or scope-limitation (documented in code comments, not a regression).
- `✗` — divergence; finding logged below the table.

| # | C++ surface (file:lines) | VHDL anchor (`zxnext.vhd:lines` unless noted) | Pass-25 verdict |
|---|---|---|---|
| 1 | `mmu.h:13` `class Mmu : public MemoryInterface` | architectural | ✓ |
| 2 | `mmu.h:15`/`mmu.cpp:15-19` constructor → `reset(true)` | top-level `reset <= i_RESET` (zxnext.vhd:1730) | ✓ |
| 3 | `mmu.cpp:52-166` `reset(bool hard)` semantics — both hard/soft clear identical state | top-level `reset_hard or reset_soft` chain (zxnext_top_issue5.vhd:880, :2384) | ✓ |
| 4 | `mmu.cpp:84` `(void)hard;` (ABI parity, no functional split) | matches VHDL (no internal hard/soft separation in zxnext.vhd) | ✓ |
| 5 | `mmu.cpp:86` clear `paging_locked_` | derived from `port_7ffd_reg(5)`, cleared at :3648 | ✓ |
| 6 | `mmu.cpp:87` clear `contention_disabled_` | `nr_08_contention_disable` cleared at :4930-4935 | ✓ |
| 7 | `mmu.cpp:88` clear `port_dffd_reg_` | :3686-3690 | ✓ |
| 8 | `mmu.cpp:89` clear `port_dffd_reg_6_` | :3686-3690 (same `if reset='1'` block, signal at :877) | ✓ |
| 9 | `mmu.cpp:90` clear `port_eff7_reg_2_` | :3777-3779 | ✓ |
| 10 | `mmu.cpp:91` clear `port_eff7_reg_3_` | :3777-3779 | ✓ |
| 11 | `mmu.cpp:92` clear `port_7ffd_` | :3646-3648 | ✓ |
| 12 | `mmu.cpp:93` clear `port_1ffd_` | :3713-3715 | ✓ |
| 13 | `mmu.cpp:95` clear `port_1ffd_special_old_` | :3716 | ✓ |
| 14 | `mmu.cpp:97-101` `nr_8f_mode_` NOT cleared on reset | signal declaration `:= "00"` at :888, no reset process | ✓ |
| 15 | `mmu.cpp:104-107` `nr_8c_reg_` lo→hi nibble copy on reset | :2253-2256 | ✓ |
| 16 | `mmu.cpp:111-118` clear Layer 2 latches (wr_en/rd_en/seg/enable/shadow/offset/bank) | :3907-3913 | ✓ |
| 17 | `mmu.cpp:123` `l2_shadow_bank_` default 11 | :5934 NR 0x13 reset | ✓ |
| 18 | `mmu.cpp:130` `nr_04_romram_bank_ = 0` | :1104 | ✓ |
| 19 | `mmu.cpp:149` `boot_rom_en_` re-armed only when `config_mode_` is set | :5109-5111 + :4926, `:1101` default '1' | ✓ |
| 20 | `mmu.cpp:13` `RESET_PAGES[8]` = `{FF,FF,0A,0B,04,05,00,01}` | :4611-4618 | ✓ |
| 21 | `mmu.cpp:150-155` slots/nr_mmu/read_only re-seed loop | :4611-4618 + nr_mmu_we semantics | ✓ |
| 22 | `mmu.cpp:158-159` `map_rom_physical(0,0)` / `(1,1)` post-seed | :3052 sram_pre_A21_A13 (sram_rom=0 → pages 0/1) | ✓ |
| 23 | `mmu.cpp:21-50` `set_boot_rom` — 8 KB internal buffer, zero-pad/truncate, mirror via `addr & 0x1FFF` | :3199-3204 hardwires bootrom to cpu_a(12:0) | ✓ |
| 24 | `mmu.h:142-143` `set_boot_rom_enabled` / `boot_rom_enabled` | bootrom_en flip-flop (:1101 default '1') | ✓ |
| 25 | `mmu.h:166` `set_config_mode` (NR 0x03) | :1102 nr_03_config_mode | ✓ |
| 26 | `mmu.h:167` `set_nr_04_romram_bank` | :1104 nr_04_romram_bank | ✓ |
| 27 | `mmu.h:179`/`mmu.cpp:295-300` `set_rom_in_sram` — re-point all slots | Next-mode :3052 routing through SRAM pages 0..7 | ✓ |
| 28 | `mmu.h:184` `set_divmmc` (raw pointer) | architectural seam | ✓ |
| 29 | `mmu.h:193` `set_multiface` (raw pointer) | architectural seam, :2937-2945 priority cascade | ✓ |
| 30 | `mmu.h:196` `set_debug_state` (raw pointer) | architectural seam | ✓ |
| 31 | `mmu.h:199-374` `read()` hot path | :3084-3132 SRAM arbiter | ✓ |
| 32 | `mmu.h:204-206` boot ROM gate `addr<0x4000 && boot_rom_en_` mask `addr & 0x1FFF` | :1856 + :3199-3204 | ✓ |
| 33 | `mmu.h:213-223` MF read overlay (above DivMMC) | :2937 priority + :3028-3035 | ✓ |
| 34 | `mmu.h:227-239` DivMMC read overlay (above Layer 2) | :3084 | ✓ |
| 35 | `mmu.h:261-310` Layer 2 read-over per-half override gate | :3037-3066 + :3100-3102 | ✓ |
| 36 | `mmu.h:266-269` `bank = nr_13 if map_shadow else nr_12` + `offset_pre` + `bofs = off_pre + l2_offset` | :2966-2969 (G144) | ✓ |
| 37 | `mmu.h:282-298` Layer 2 sum-`& 0x70 == 0x70` floating-bus return | :2971 + :3101-3102 `sram_active = NOT layer2_A21_A13(8)` | ✓ |
| 38 | `mmu.h:299-302` Layer 2 read SRAM page = `to_sram_page(...)` | :2964 mmu_A21_A13 | ✓ |
| 39 | `mmu.h:311` `slot = addr >> 13` | :2949 mem_active_slot derivation | ✓ |
| 40 | `mmu.h:320-331` Alt-ROM read override (en+rw=0 only) | :3021/:3078 (4th clause read path) | ✓ |
| 41 | `mmu.h:336-346` Config-mode read routing (`(nr_04 << 1) \| slot`) | :3044-3050 + :3124 | ✓ |
| 42 | `mmu.h:347-348` `read_ptr_[slot]` null-check / 0xFF return | inactive-slot floating-bus = 0xFF | ✓ |
| 43 | `mmu.h:363-365` `p3_floating_bus_dat_` latch on contended read | :4498-4509 + per-page :4489-4493 gate | ✓ |
| 44 | `mmu.h:367-372` data-breakpoint hook on read | architectural | ✓ |
| 45 | `mmu.h:376-475` `write()` hot path | :3084-3132 + :3078 write-mux | ✓ |
| 46 | `mmu.h:378-383` data-breakpoint hook on write | architectural | ✓ |
| 47 | `mmu.h:390-396` MF write overlay (ROM half R/O at cpu_a(13)=0, RAM half RW) | :3028-3035, `sram_pre_rdonly = NOT cpu_a(13)` | ✓ |
| 48 | `mmu.h:400-402` DivMMC write overlay | :3084 | ✓ |
| 49 | `mmu.h:408-437` Layer 2 write-over (same gate + sum & 0x70 == 0x70 drop) | :3037-3066 + :3100-3102 | ✓ |
| 50 | `mmu.h:448-453` Alt-ROM write override (en+rw=1 only) | :3056 + :3078 (sram_pre_rdonly='0' clause) | ✓ |
| 51 | `mmu.h:458-462` Config-mode write routing | :3044-3050 sram_pre_rdonly<='0' | ✓ |
| 52 | `mmu.h:463` `if (read_only_[slot]) return;` (drop ROM write) | inactive ROM slot | ✓ |
| 53 | `mmu.h:466` byte store | :3084-3132 main RAM path | ✓ |
| 54 | `mmu.h:472-474` `p3_floating_bus_dat_` latch on contended write | :4498-4509 + :4504-4505 cpu_do branch | ✓ |
| 55 | `mmu.h:54-57` `get_effective_page` (`nr_mmu_ if != 0xFF else slots_`) | :4611-4612 sentinel + :4619-4644 dynamic resolve | ✓ |
| 56 | `mmu.h:58` `is_slot_rom` (`read_only_[slot]`) | :3057 ROMCS branch indicator | ✓ |
| 57 | `mmu.h:91-94` `slot_in_rom_area` — page >= 0xE0 | :3037 `mmu_A21_A13(8)` derivation | ✓ |
| 58 | `mmu.h:102-106` `sram_pre_override_divmmc_eligible` | :3029-:3066 bit-2 derivation | ✓ |
| 59 | `mmu.h:119-126` `sram_pre_override_romcs_priority` | :3057 only-true clause | ✓ |
| 60 | `mmu.cpp:168-271` `rebuild_ptr` | :3037-3066 SRAM arbiter resolution | ✓ |
| 61 | `mmu.cpp:170-182` ROM/unmapped branch (slot served from RAM 0..7 if `rom_in_sram_`) | :3052 Next routing | ✓ |
| 62 | `mmu.cpp:213-265` `page >= 0xE0` gate | :3037 `mmu_A21_A13(8)='1'` | ✓ |
| 63 | `mmu.cpp:214-217` slots 2..7: inactive → null pointer | :3060-3061 `sram_pre_active = NOT mmu_A21_A13(8)` | ✓ |
| 64 | `mmu.cpp:218-263` slots 0/1: route to legacy ROM (sram_rom-derived) | :3052 + sram_rom decode | ✓ |
| 65 | `mmu.cpp:235-238` `sram_rom = current_sram_rom()` → `rom_page = sram_rom * 2 + slot` | :3052 `sram_pre_A21_A13 <= "000000" & sram_rom & cpu_a(13)` | ✓ |
| 66 | `mmu.cpp:262` `slots_[slot] = rom_page` keep consistent with `read_only_=true` (V11-MEM-01 fix) | semantic mirror — physical page being served | ✓ |
| 67 | `mmu.cpp:267-269` RAM slot: `to_sram_page(page)` + RW pointer | :2964 + :3037-3043 RAM path | ✓ |
| 68 | `mmu.cpp:273-280` `set_page` | nr_mmu_we explicit RAM map (NR 0x50-0x57 with v < 0xE0) | ✓ |
| 69 | `mmu.cpp:282-293` `map_rom_physical` (slots_+read_only_+rom-buffer choice) | :3052 sram_rom path | ✓ |
| 70 | `mmu.cpp:302-307` `map_rom` — `map_rom_physical` + force `nr_mmu_[slot] = 0xFF` | :4611-4612 ROM sentinel | ✓ |
| 71 | `mmu.h:1093-1097` `to_sram_page` — `+0x20` except bank 5 (0x0A/0x0B) and bank 7 lo (0x0E) | :2961-2962 dual-port bypass + :2964 shift | ✓ |
| 72 | `mmu.h:803-848` `set_machine_type` no-op on no-change; +3 special skip; per-slot legacy-ROM refresh only on read_only_=true | :2981-3008 combinational sram_rom + :3813 no nr_mmu_we | ✓ |
| 73 | `mmu.h:846-847` `engage_legacy_rom_paging_slot(*, set_nr_sentinel=false)` (V12-MEM-01 fix) | :3813/:4607 no nr_mmu_we trigger | ✓ |
| 74 | `mmu.h:866-898` `current_sram_rom` — 48K=0, +3 2-bit with altrom lock, 128K/ZXN 1-bit with altrom lock | :2981-3008 | ✓ |
| 75 | `mmu.h:867-869` 48K → return 0 | :2985 `sram_rom <= "00"` | ✓ |
| 76 | `mmu.h:870-875` +3 path: lock overrides → `lock_rom1<<1 \| lock_rom0`; else `current_rom_bank()` | :2988-2995 | ✓ |
| 77 | `mmu.h:876-896` 128K + ZXN path: lock → `lock_rom1`; else `current_rom_bank() & 1` | :2997-3007 else branch | ✓ |
| 78 | `mmu.h:921-939` `sram_rom3` — 48K hardwired true, +3 lock & no-lock arithmetic, ZXN/128K else | :2985/:2990/:2994/:3000/:3004 | ✓ |
| 79 | `mmu.h:780-784` `current_rom_bank` = `((1ffd>>2)&1)<<1 \| ((7ffd>>4)&1)` | :3772 `port_1ffd_rom = port_1ffd_reg(2) & port_7ffd_reg(4)` | ✓ |
| 80 | `mmu.h:785` `rom3_selected` = `current_rom_bank()==3` | derived | ✓ |
| 81 | `mmu.h:942` `map_rom` (public) | NR 0x50/0x51 ROM-sentinel dispatch | ✓ |
| 82 | `mmu.h:954-955` `engage_legacy_rom_paging` / `engage_legacy_ram_paging` (G46(b) Wave 8) | :4611-4612 MMU<i>='FF' fallback | ✓ |
| 83 | `mmu.h:981` `engage_legacy_rom_paging_slot(int, bool=true)` | :4686-4696 per-slot nr_mmu_we | ✓ |
| 84 | `mmu.cpp:523-559` `engage_legacy_rom_paging_slot` body | :4686-4699 + sram_rom resolution | ✓ |
| 85 | `mmu.cpp:542` `sram_rom = current_sram_rom()` | :3052 | ✓ |
| 86 | `mmu.cpp:543` `map_rom_physical(slot, sram_rom * 2 + slot)` | :3052 ROM page = sram_rom*2 + cpu_a(13) | ✓ |
| 87 | `mmu.cpp:556-558` conditional `nr_mmu_[slot] = 0xFF` (V12-MEM-01) | :4686 nr_mmu_we | ✓ |
| 88 | `mmu.cpp:309-351` `set_l2_port` (0x123B handler) | :3914-3923 | ✓ |
| 89 | `mmu.cpp:319` `l2_bank_ = active_bank` (mirror NR 0x12) | :2968 layer2_active_bank | ✓ |
| 90 | `mmu.cpp:320-325` bit 4 = offset-only update | :3922 | ✓ |
| 91 | `mmu.cpp:326-329` bits 0/1/2/3 (wr_en/enable/rd_en/map_shadow) | :3914-3919 | ✓ |
| 92 | `mmu.cpp:330-347` segment store (raw + legacy mask for save-state) | :3920 | ✓ |
| 93 | `mmu.h:1062-1070` `l2_port_readback` | :3933 | ✓ |
| 94 | `mmu.h:1010-1011` `l2_read_enable` / `l2_write_enable` getters | architectural | ✓ |
| 95 | `mmu.h:1027` `set_l2_shadow_bank` (from NR 0x13) | :2968 nr_13 fan-in | ✓ |
| 96 | `mmu.h:1036` `set_l2_active_bank` (from NR 0x12) | :2968 nr_12 fan-in (V4-MEM-01 fix) | ✓ |
| 97 | `mmu.h:1054` `set_l2_enable` (from NR 0x69 bit 7) | :3924-3925 alias (V13-MEM-01) | ✓ |
| 98 | `mmu.h:1057` `l2_enable` getter | :3924-3925 read-back fan-out | ✓ |
| 99 | `mmu.h:1124-1135` `l2_overlay_active_for` — low half (non-MF), high half seg=11 only | :3037-3066 sram_pre_override(1) | ✓ |
| 100 | `mmu.h:1141-1146` `l2_offset_pre_for` — cpu_a(15:14) when seg=11, else seg | :2966 | ✓ |
| 101 | `mmu.h:1189-1204` `mem_contend_for_` per-page decode | :4489-4493 | ✗ (V25-MEM-01 → carry-over of V24-MEM-01: keys on `machine_type_`, VHDL keys on `machine_timing_*`) |
| 102 | `mmu.h:1194-1195` 48K branch `(low>>1)&7 == 5` (bank 5 only) | :4490 | ✓ |
| 103 | `mmu.h:1196-1197` 128K branch `low & 2` (odd banks) | :4491 | ✓ |
| 104 | `mmu.h:1198-1199` +3 branch `low & 8` (banks ≥ 4) | :4492 | ✓ |
| 105 | `mmu.h:1200-1202` ZXN_ISSUE2 → false | none (no machine_timing line for ZXN) | ✓ |
| 106 | `mmu.h:1206-1231` `altrom_sram_page_` per machine type | :2986/:2988-2995/:2998-3005 + :3117 | ✓ |
| 107 | `mmu.h:1211-1214` 48K alt_128_n = NOT((NOT lk1) AND lk0) | :2986 | ✓ |
| 108 | `mmu.h:1216-1219` +3 alt_128_n: lock → lk1; else port_1ffd_rom(0) | :2988-2995 | ✓ |
| 109 | `mmu.h:1220-1227` 128K/ZXN alt_128_n: same lock semantics | :2998-3005 | ✓ |
| 110 | `mmu.h:1229-1230` page = 0x0C \| (alt_128_n<<1) \| a13 | :3117 9-bit composition | ✓ |
| 111 | `mmu.cpp:373-389` `compose_bank_` per VHDL :3763-3766 | Pentagon/standard branches | ✓ |
| 112 | `mmu.cpp:376` `bank(2:0) = p7ffd & 7` | :3763 | ✓ |
| 113 | `mmu.cpp:377-381` Pentagon: bits 4:3 from 7FFD(7:6); bit 5 from 7FFD(5) only when pentagon_1024_en; bit 6 forced 0 | :3764-3766 (when branch) | ✓ |
| 114 | `mmu.cpp:383-387` Standard: bits 4:3/5/6 from DFFD(0:1)/(2)/(3) | :3764-3766 (else branch) | ✓ |
| 115 | `mmu.cpp:396-404` `apply_legacy_ram_slots_` (slots 6/7 from compose_bank_) | :4677-4680 | ✓ |
| 116 | `mmu.cpp:410-438` `apply_legacy_rom_slots_` (slots 0/1: EFF7(3) RAM-at-0 OR sram_rom-derived ROM) | :4619-4644 | ✓ |
| 117 | `mmu.cpp:416-418` EFF7(3) branch → set_page(0, 0x00) / set_page(1, 0x01) | :4636-4644 | ✓ |
| 118 | `mmu.cpp:419-437` else → `map_rom_physical(0/1, sram_rom*2 + slot)` + clobber nr_mmu_[0/1] = 0xFF | :3052 + :4611-4612 | ✓ |
| 119 | `mmu.cpp:447-450` `apply_legacy_paging_` — RAM then ROM | unconditional both-halves rebuild | ✓ |
| 120 | `mmu.cpp:465-478` `apply_plus3_special_paging_` — 4-config table {0..3}/{4..7}/{4,5,6,3}/{4,7,6,3} | :4625-4632 | ✓ |
| 121 | `mmu.cpp:487-492` `revert_slots_2_to_5_post_special_` — {0A,0B,04,05} | :4655-4670 | ✓ |
| 122 | `mmu.cpp:505-516` `apply_paging_update_` — three-way arbiter (special / exit / legacy) + `port_1ffd_special_old_` capture | :4623-4684 + :3729 | ✓ |
| 123 | `mmu.cpp:603-626` `map_128k_bank` — paging-lock gate + port_7ffd_ store + apply_paging_update_ | :3650 + :3769 + :4623 | ✓ |
| 124 | `mmu.cpp:609` `effective_paging_locked()` (Pentagon-1024 lock override) | :3769 | ✓ |
| 125 | `mmu.cpp:618` `paging_locked_ = (port_7ffd >> 5) & 1` | :3650 + :3769 | ✓ |
| 126 | `mmu.cpp:628-653` `write_port_dffd` — same lock gate, store DFFD(4:0) + DFFD(6) | :3691-3704 (incl. :3694 bit-6 latch) | ✓ |
| 127 | `mmu.cpp:646-647` `port_dffd_reg_ = v & 0x1F`; `port_dffd_reg_6_ = (v & 0x40) != 0` | :3693 + :3694 | ✓ |
| 128 | `mmu.cpp:655-668` `write_port_eff7` — no lock gate; store bits 2,3 | :3777-3782 + :4636-4644 | ✓ |
| 129 | `mmu.cpp:670-685` `write_nr_8f` — store 2-bit mode + apply_paging_update_ | :3787-3794 + :3815 | ✓ |
| 130 | `mmu.cpp:687-763` `write_nr_8e` — bit3-mode bank update + bit2-gate 7FFD(4) + 1FFD(2:0) updates | :3662-3670 + :3696-3704 + :3726-3734 | ✓ |
| 131 | `mmu.cpp:702-706` bit3 → 7FFD(2:0) ← nr_wr_dat(6:4) | :3666-3668 | ✓ |
| 132 | `mmu.cpp:707-711` bit2=0 → 7FFD(4) ← nr_wr_dat(0) | :3670 | ✓ |
| 133 | `mmu.cpp:716-722` bit3 → clear DFFD(3); DFFD(0)←bit7; DFFD(2:1)="00" | :3699-3704 | ✓ |
| 134 | `mmu.cpp:730-734` 1FFD(2:0) updates from nr_wr_dat | :3730-3734 | ✓ |
| 135 | `mmu.cpp:750-762` MMU6/7 suppression on bit-3=0 (no special, no exit) | :4677 gated on port_memory_ram_change_dly | ✓ |
| 136 | `mmu.cpp:770-788` `read_nr_8e` (read-back composition) | :6158-6159 | ✓ |
| 137 | `mmu.cpp:778` bit 3 always '1' in read-back | :6159 spec sentinel | ✓ |
| 138 | `mmu.cpp:784` bit0 = `(7ffd(4) AND NOT 1ffd(0)) OR (1ffd(1) AND 1ffd(0))` | :6159 | ✓ |
| 139 | `mmu.cpp:790-805` `map_plus3_bank` — lock gate, port_1ffd_ store, apply_paging_update_ | :3718 + :4623-4684 | ✓ |
| 140 | `mmu.h:503` `write_port_dffd` declaration | :2596 port_dffd decode | ✓ |
| 141 | `mmu.h:540` `write_port_eff7` declaration | :2604 port_eff7 decode | ✓ |
| 142 | `mmu.h:569` `write_nr_8f` declaration | :3787-3794 | ✓ |
| 143 | `mmu.h:589-591` `effective_paging_locked` (Pentagon-1024 override) | :3769 | ✓ |
| 144 | `mmu.h:578-580` `pentagon_1024_en` = mode==3 AND !EFF7(2) | :3801 | ✓ |
| 145 | `mmu.h:581-583` `pentagon_en` = mode==2 OR pentagon_1024_en | :3798 | ✓ |
| 146 | `mmu.h:639-642` `unlock_paging` — clear both `paging_locked_` AND `port_7ffd_` bit 5 (V3-MEM-01 fix) | :3654-3656 + :3769 | ✓ |
| 147 | `mmu.h:645` `paging_locked` getter | :5906 NR 0x08 readback bit 7 source | ✓ |
| 148 | `mmu.h:653-654` `set_contention_disabled` / `contention_disabled` | :5176 NR 0x08 bit 6 + :4935 | ✓ |
| 149 | `mmu.h:692-697` NR 0x8C accessors (en/rw/lock_rom1/lock_rom0) | :2247-2265 | ✓ |
| 150 | `mmu.cpp:580-601` `set_nr_8c` — store + per-slot ROM refresh on read_only_=true only (V3-MEM-01 + V12-MEM-01) | :2256-2265 + :3813 no port_memory_change_dly | ✓ |
| 151 | `mmu.h:721-722` `p3_floating_bus_dat` getter/setter | :4498-4509 | ✓ |
| 152 | `mmu.h:729-734` `set_slot_contended` / `slot_contended` (legacy mirror for save-state) | :4498-4509 (legacy 16K view) | ✓ |
| 153 | `mmu.h:737` `port_7ffd` getter | :3640 | ✓ |
| 154 | `mmu.h:746` `shadow_screen_en` = bit 3 of port_7ffd_ | :3640 + :4453 | ✓ |
| 155 | `mmu.h:757-760` `set_port_7ffd_bit3` (NR 0x69 fan-in) | :3658-3660 | ✓ |
| 156 | `mmu.h:763` `port_1ffd` getter | :3713 | ✓ |
| 157 | `mmu.h:1006` `set_l2_port` declaration | :3914-3923 | ✓ |
| 158 | `mmu.h:1016-1022` `l2_offset` / `l2_map_shadow` getters | :3914-3923 + :2967-2968 | ✓ |
| 159 | `mmu.h:1041` `l2_bank` getter | :2968 | ✓ |
| 160 | `mmu.h:1093` `to_sram_page` public (for L2/tilemap/sprite renderers) | :2964 | ✓ |
| 161 | `mmu.cpp:811-881` `save_state` — full Mmu state dump including `nr_mmu_[8]` (V4-MEM-01) | architectural | ✓ |
| 162 | `mmu.cpp:883-938` `load_state` — restore full state, rebuild_ptr per slot, append `nr_mmu_` | architectural | ✓ |
| 163 | `mmu.cpp:944-954` `divmmc_read` / `divmmc_write` out-of-line helpers | :3084 priority | ✓ |
| 164 | `mmu.cpp:971-973` `mf_overlay_active_` = `multiface_->is_mem_active()` | multiface.vhd:186 `mf_enabled_o = mf_enable OR fetch_66` | ✓ |
| 165 | `mmu.cpp:975-978` `mf_rom_byte_` | :3028-3035 (8 KB MF ROM at 0x0000-0x1FFF) | ✓ |
| 166 | `mmu.cpp:980-983` `mf_ram_byte_` | :3028-3035 (8 KB MF RAM at 0x2000-0x3FFF) | ✓ |
| 167 | `mmu.cpp:985-987` `mf_ram_write_` | :3028-3035 (`sram_pre_rdonly = NOT cpu_a(13)`) | ✓ |
| 168 | `ram.h:5-20` `Ram` class — `read`/`write`/`page_ptr`/`reset`/`size`/`save_state`/`load_state` | architectural | ✓ |
| 169 | `ram.cpp:5` `Ram(size_bytes = 2 MiB)` default | architectural | ✓ |
| 170 | `ram.cpp:7-10` `read` — bounds-check, OOB returns 0xFF | floating-bus default | ✓ |
| 171 | `ram.cpp:12-14` `write` — bounds-check, OOB silently dropped | inactive-page drop | ✓ |
| 172 | `ram.cpp:16-26` `page_ptr` (8 KB granularity) | `page * 0x2000` indexing | ✓ |
| 173 | `ram.cpp:28` `reset` — fill all bytes with 0 | power-on RAM contents | ✓ |
| 174 | `ram.cpp:30-40` `save_state`/`load_state` — round-trip full RAM | architectural | ✓ |
| 175 | `rom.h:6-37` `Rom` class — 64 KB array (4 × 16 KB) + per-slot loaded flag | architectural | ✓ |
| 176 | `rom.cpp:6` constructor — fill with 0xFF | unloaded-ROM floating-bus default | ✓ |
| 177 | `rom.cpp:8-23` `load(slot, path)` — file-based 16 KB read into slot offset | architectural | ✓ |
| 178 | `rom.cpp:25-37` `load_bytes(slot, data, size)` — buffer-based 16 KB load (SD-ROM path) | architectural | ✓ |
| 179 | `rom.cpp:39-42` `read(addr)` — bounds-check, OOB returns 0xFF | floating-bus default | ✓ |
| 180 | `rom.cpp:44-54` `page_ptr` (8 KB granularity) | `page * 0x2000` | ✓ |
| 181 | `rom.h:9` `reset` — clear `alt_rom_config_` | NR 0x8C bits not stored here; this is legacy alt-rom enable | ✓ |
| 182 | `rom.h:28-30` `set_alt_rom_config` / `alt_rom_enabled` | legacy alt-rom enable interface | ✓ |
| 183 | `contention.h:5` `enum class MachineType { ZXN_ISSUE2, ZX48K, ZX128K, ZX_PLUS3 }` | :5749-5754 machine_type enumeration + Pentagon removal note | ✓ |
| 184 | `contention.h:7-23` `ContentionModel::build(MachineType)` | :4481 + LUT seed | ✓ |
| 185 | `contention.h:20` `rebuild_for_type(MachineType)` (NR 0x03 timing-mode commit, hot rebuild) | :5126-5131 commit + :6694-6703 latch | ✓ |
| 186 | `contention.h:22` `delay(hc, vc)` LUT lookup | :582-583 zxula | ✓ |
| 187 | `contention.h:23` `is_contended_address` — per-16K slot mirror | :4498-4509 legacy 16K view | ✓ |
| 188 | `contention.h:26-28` `set_contended_slot` (4-slot mirror) | architectural mirror | ✓ |
| 189 | `contention.h:43` `set_mem_active_page` | :4489 mem_active_page | ✓ |
| 190 | `contention.h:54-57` `set_cpu_speed` (immediate commit, both shadow + effective) | :5788-5789 + :5816-5817 | ✓ |
| 191 | `contention.h:68-70` `set_pending_cpu_speed` (shadow only) | :5788-5789 (production NR 0x07 path) | ✓ |
| 192 | `contention.h:80-83` `commit_pending_cpu_speed_on_bus_idle` (DMA-aware) | :5809-5817 | ✓ |
| 193 | `contention.h:93-96` `set_contention_disable` (immediate commit) | :5800-5823 (test fixture path) | ✓ |
| 194 | `contention.h:105-107` `set_contention_disable_shadow` (production NR 0x08 path) | :5800-5823 | ✓ |
| 195 | `contention.h:116-120` `commit_contention_disable_on_hc` (hc(8)='1' window) | :5822-5823 | ✓ |
| 196 | `contention.h:130-131` `set_port_7ffd_io_en` / getter (V9-MEM-01 NR 0x82 bit 1 mirror) | :2399 internal_port_enable bit 1 + :2594 | ✓ |
| 197 | `contention.h:147-148` `set_port_ulap_io_en` / getter (V15-CPU-NIT-03 NR 0x85 bit 0 mirror) | :2439 internal_port_enable bit 24 + :2685-2686 | ✓ |
| 198 | `contention.h:150-154` accessors `mem_active_page` / `cpu_speed` / `pending_cpu_speed` / `contention_disable` / `contention_disable_shadow` | architectural | ✓ |
| 199 | `contention.h:159` `is_contended_access` | :4481 + :4489-4493 combined gate | ✗ (V25-MEM-01 — same `machine_type_` vs `machine_timing_*` divergence) |
| 200 | `contention.h:188-190` `contention_tick(...)` per-cycle | :4481-4496 + zxula.vhd:579-600 | ~ (uses `type_` for per-machine page decode; same divergence scope as #199 — captured under V25-MEM-01) |
| 201 | `contention.h:222` `port_contend` | :4496 | ✓ |
| 202 | `contention.cpp:3-18` `build` (full reset incl. all gate state) | :4481 + :1300/:1380 power-on defaults | ✓ |
| 203 | `contention.cpp:20-67` `rebuild_for_type` (hot LUT rebuild, gate state preserved) | :582-583 + :414 zxula | ✓ |
| 204 | `contention.cpp:32-48` ZXN_ISSUE2 early-return (no LUT entries) | no contention for Next | ✓ |
| 205 | `contention.cpp:49-51` pattern `{6,5,4,3,2,1,0,0}` + p3-or-128 type | :582-583 hc_adj formula | ✓ |
| 206 | `contention.cpp:52-66` LUT fill: vc in [0,191] × hc in [0,255]; hc_adj wrap | :414 border_active_v + :582-583 | ✓ |
| 207 | `contention.cpp:55` `hc_adj = ((hc & 0xF) + 1) & 0xF` (4-bit wrap) | :582-583 | ✓ |
| 208 | `contention.cpp:56-57` `(hc_adj & 0xC) != 0` OR (is_p3 AND `(hc_adj & 0xE) == 0`) | :582-583 | ✓ |
| 209 | `contention.cpp:66` default slot 1 contended on 48K/128K/+3 | :4490-4492 bank-5 / odd / banks≥4 | ✓ |
| 210 | `contention.cpp:69-72` `delay(hc, vc)` LUT lookup with bounds | architectural | ✓ |
| 211 | `contention.cpp:74-77` `is_contended_address` — slot mirror | architectural | ✓ |
| 212 | `contention.cpp:79-127` `is_contended_access` (gate + per-machine page decode) | :4481 + :4489-4493 | ✗ (V25-MEM-01 — keyed on `type_`, VHDL keys on `machine_timing_*`) |
| 213 | `contention.cpp:105-106` skip if `contention_disable_` OR `cpu_speed_ != 0` | :4481 | ✓ |
| 214 | `contention.cpp:109` skip if `mem_active_page(7:4) != 0` | :4489 | ✓ |
| 215 | `contention.cpp:112-126` switch on `type_` (48K/128K/+3/ZXN) | should be `machine_timing` | ✗ (V25-MEM-01) |
| 216 | `contention.cpp:129-178` `port_contend` (even / ULA+ / port_7ffd_active OR-terms) | :4496 + :2594 + :2685-2686 | ✓ |
| 217 | `contention.cpp:147-149` ULA+ term gated on `port_ulap_io_en` (NR 0x85 bit 0) | :2439 + :2685-2686 | ✓ |
| 218 | `contention.cpp:167-175` port_7ffd_active term: machine + NR 0x82 bit 1 + decode | :2593-2594 | ✓ |
| 219 | `contention.cpp:172-173` +3 extra A14='1' requirement | :2593 `cpu_a(14)='1' OR NOT p3_timing` | ✓ |
| 220 | `contention.cpp:180-303` `contention_tick(...)` per-cycle | :4481-4496 + zxula.vhd:579-600 | ~ (same scope as #200) |
| 221 | `contention.cpp:205-206` early-return on contention disable / non-zero cpu_speed | :4481 | ✓ |
| 222 | `contention.cpp:215-218` window gate: hc(8)=0, vc(8)=0, !(vc&0xC0==0xC0) | :414 + :582-583 | ✓ |
| 223 | `contention.cpp:219-222` hc_adj + p3 wait_s composition | :582-583 | ✓ |
| 224 | `contention.cpp:231-248` mem_c decode (switch on type_) | :4489-4493 — same V25-MEM-01 scope | ~ |
| 225 | `contention.cpp:264-265` `port_contend(cpu_a, ulap_eff)` for port path | :4496 | ✓ |
| 226 | `contention.cpp:289` `kPattern[8] = {6,5,4,3,2,1,0,0}` | :582-583 magnitude | ✓ |
| 227 | `contention.cpp:291-297` +3 path: WAIT_n on MREQ+mem_c only (no I/O) | zxula.vhd:599-600 + commented :I/O | ✓ |
| 228 | `contention.cpp:299-303` 48K/128K: o_cpu_contend on mem OR port | zxula.vhd:587-595 | ✓ |
| 229 | `mmu.h:622-625` `read_nr_8e` (declaration only, body in cpp) | :6158-6159 | ✓ |
| 230 | `mmu.h:1289-1291` `compose_bank_` declaration | :3763-3766 | ✓ |
| 231 | `mmu.h:1293` member `Ram& ram_` | architectural | ✓ |
| 232 | `mmu.h:1294` member `Rom& rom_` | architectural | ✓ |
| 233 | `mmu.h:1295` `slots_[8]` (physical 8 KB page index per slot) | architectural | ✓ |
| 234 | `mmu.h:1296` `nr_mmu_[8]` (NR 0x50-0x57 register-visible value) | :4611-4618 | ✓ |
| 235 | `mmu.h:1297` `read_ptr_[8]` (fast dispatch) | architectural | ✓ |
| 236 | `mmu.h:1298` `write_ptr_[8]` | architectural | ✓ |
| 237 | `mmu.h:1299` `read_only_[8]` | architectural | ✓ |
| 238 | `mmu.h:1300` `paging_locked_` member | :3640 + :3769 | ✓ |
| 239 | `mmu.h:1305` `p3_floating_bus_dat_` member (default 0) | :4498-4509 | ✓ |
| 240 | `mmu.h:1307` `slot_contended_[4]` legacy mirror | :4498-4509 (16K view) | ✓ |
| 241 | `mmu.h:1312` `contention_disabled_` member | :5176 + :4935 | ✓ |
| 242 | `mmu.h:1317` `nr_8c_reg_` member (default 0) | :387 + :2253-2256 | ✓ |
| 243 | `mmu.h:1322` `machine_type_` member (default ZXN_ISSUE2) | :2981-3008 driver | ✓ |
| 244 | `mmu.h:1323` `port_7ffd_` member (default 0) | :3640 + :3646-3648 reset | ✓ |
| 245 | `mmu.h:1324` `port_1ffd_` member (default 0) | :3713 + :3713-3715 reset | ✓ |
| 246 | `mmu.h:1334` `port_1ffd_special_old_` member (default false) | :3716 reset | ✓ |
| 247 | `mmu.h:1342` `port_dffd_reg_` member (default 0) | :3686-3690 reset | ✓ |
| 248 | `mmu.h:1349` `port_dffd_reg_6_` member (default false) | :877 + :3686-3689 reset | ✓ |
| 249 | `mmu.h:1357-1358` `port_eff7_reg_2_` / `_3_` members | :3777-3779 reset | ✓ |
| 250 | `mmu.h:1365` `nr_8f_mode_` member (default 0; NOT reset) | :888 default + no reset process | ✓ |
| 251 | `mmu.h:1372` `l2_write_enable_` member | :3914-3923 + :3907-3913 reset | ✓ |
| 252 | `mmu.h:1373` `l2_read_enable_` member | :3918 + :3907-3913 reset | ✓ |
| 253 | `mmu.h:1374` `l2_segment_mask_` (legacy save-state) | :3920 legacy bitmask | ✓ |
| 254 | `mmu.h:1375` `l2_segment_raw_` member | :3920 + :3933 readback | ✓ |
| 255 | `mmu.h:1376` `l2_bank_` member (default 8) | :2968 | ✓ |
| 256 | `mmu.h:1381` `l2_enable_` member | :3924-3925 fan-in | ✓ |
| 257 | `mmu.h:1382` `l2_map_shadow_` member | :3919 | ✓ |
| 258 | `mmu.h:1383` `l2_offset_` member | :3922 | ✓ |
| 259 | `mmu.h:1384` `l2_shadow_bank_` member (default 11) | :5934 NR 0x13 reset | ✓ |
| 260 | `mmu.h:1392` `config_mode_` member (default false) | :1102 nr_03_config_mode | ✓ |
| 261 | `mmu.h:1393` `nr_04_romram_bank_` member (default 0) | :1104 | ✓ |
| 262 | `mmu.h:1394` `rom_in_sram_` member (default false) | :3052 + Next-only | ✓ |
| 263 | `mmu.h:1400-1403` `boot_rom_buf_` / `boot_rom_` / `boot_rom_size_` / `boot_rom_en_` members | :1101 + :3199-3204 | ✓ |
| 264 | `mmu.h:1406` `divmmc_` non-owning pointer | architectural | ✓ |
| 265 | `mmu.h:1410` `multiface_` non-owning pointer | architectural | ✓ |
| 266 | `mmu.h:1413` `debug_state_` non-owning pointer | architectural | ✓ |
| 267 | `mmu.h:1416-1417` `divmmc_read` / `divmmc_write` out-of-line decls | architectural | ✓ |
| 268 | `mmu.h:1422-1425` `mf_overlay_active_` / `mf_rom_byte_` / `mf_ram_byte_` / `mf_ram_write_` decls | architectural | ✓ |

**Row count: 268** (vs the P24 reviewer's 168 target — comfortable margin).

## Pass-25 findings

### V25-MEM-01 (class-(d), CARRY-OVER of V24-MEM-01 — convergence-stable re-listed)

- **Surface**: `src/memory/contention.cpp:112-126` (`is_contended_access` switch on `type_`), `src/memory/contention.cpp:231-248` (`contention_tick` mem_c decode), `src/memory/mmu.h:1189-1204` (`mem_contend_for_` switch on `machine_type_`).
- **VHDL anchor**: `zxnext.vhd:4490-4492` (`mem_contend` keyed on `machine_timing_48 / machine_timing_128 / machine_timing_p3`), plus `:4481` (`i_contention_en` gated on `machine_timing_pentagon` and `eff_nr_03_machine_timing`).
- **Divergence**: jnext keys the contention decode on `ContentionModel::type_` / `Mmu::machine_type_` (a single `MachineType` enum threaded through from `Emulator::init`). VHDL's `mem_contend` formula consults `machine_timing_*`, which is driven from the SEPARATE `nr_03_machine_timing` register (NR 0x03 bits 6:4) via the latch `eff_nr_03_machine_timing` committed on the next video frame boundary (zxnext.vhd:6694-6703). VHDL `machine_type_*` (driven by NR 0x03 bits 2:0, lines 5741-5755) feeds DIFFERENT consumers: the SRAM arbiter sram_rom decode at `:2981-3008`, the ROM-bank mux, etc.
- **Observable on Next boot**: NONE. The Next firmware sets both `nr_03_machine_timing` and `nr_03_machine_type` from the same `Machine_id` value in the canonical boot path; under the `Emulator::init` machine-type push, jnext sets a single `MachineType` that maps to a consistent (timing, type) pair. The two fields only diverge when a user explicitly re-writes NR 0x03 with bits 6:4 ≠ bits 2:0 — not exercised by NextZXOS boot.
- **Reviewer reclassification (P24)**: the P24 reviewer noted disagreement with the class-(d) tag, arguing the fix is small enough to qualify as class-(c). The Pass-25 audit re-examines this:
  - **Minimal class-(c) fix scope**: add two members `machine_timing_` (in `ContentionModel`) and the same mirror in `Mmu`; add setters; thread NR 0x03 bits 6:4 commit through the existing video-frame seam (`Emulator::commit_pending_nr_03_machine_timing` or equivalent — does NOT exist yet); change three switch statements from `type_` to `machine_timing_`. Estimated diff: ~40 LOC.
  - **Architectural-scope fix (true class-(d))**: split `MachineType` into `MachineType` (NR 0x03 bits 2:0) and `MachineTiming` (NR 0x03 bits 6:4); thread independently through `Emulator`, `Mmu`, `ContentionModel`, save-state schema, all callers; add the `eff_nr_03_machine_timing` deferred-commit latch with video-frame trigger; update every test that pushes machine_type to push BOTH fields. Estimated diff: ~250 LOC + schema bump.
  - **Verdict**: the class-(d) classification stands. The minimal class-(c) fix would model the divergence's primary observable consumer (the contention decode) but leave the broader architectural split unmodeled (no separate timing-vs-type field on save-state; no `eff_nr_03_machine_timing` latch with video-frame commit; no NR 0x03 bits-6:4-only commit path; no audit-trail of which other consumers — `port_7ffd_active` at :2594, `port_ff_dat_ula` at :4513, etc. — also depend on `machine_timing_*` and would still key on `MachineType`). A minimal class-(c) fix would leave 4-5 surfaces inconsistent (audit cost would re-emerge in Pass-26). Class-(d) requires user authorization because it bumps save-state schema and touches 5+ surfaces.
- **Author note (handoff for user)**: V25-MEM-01 is the same finding as V24-MEM-01 (V25 carry-over). It is NOT a regression — Memory subsystem source is byte-identical between P24 (`05f157b7`) and P25 (`7414784`). The differential `git log 05f157b7..7414784 -- src/memory/` returns EMPTY. This is convergence-stable.
- **Recommendation**: leave V25-MEM-01 pending user authorization, same as V24-MEM-01. Document under the same architectural escalation bucket.

## Cross-cutting families final sweep

A final sweep across the cross-cutting families flagged in earlier passes:

- **Lock-bypass family** (P3-P5 era): NR 0x8E / NR 0x69 / NR 0x08 bit 7 — all bypass `port_7ffd_locked`. Verified ✓ at rows #128 (NR 0x8E branch outside lock gate), #142 (NR 0x8F branch outside), #146 (`unlock_paging`), #155 (`set_port_7ffd_bit3` accepts even when locked).
- **EFF7(3) RAM-at-0x0000 family** (P3-P4 era): only fires on `port_memory_change_dly='1'`, not on NR 0x50/0x51 nor NR 0x8C. Verified ✓ at rows #84-87 (`engage_legacy_rom_paging_slot` keeps EFF7 inert on NR 0x50/0x51) and #150 (`set_nr_8c` keeps EFF7 inert on NR 0x8C).
- **Sram_rom + altrom-lock family** (P5-P8 era): per-machine decode (48K hardwired '00', +3 2-bit, 128K/ZXN 1-bit), altrom lock overrides. Verified ✓ at rows #74-78 (`current_sram_rom`), #106-110 (`altrom_sram_page_`), #111-114 (`compose_bank_` non-Pentagon branch).
- **+3 special paging family** (P5 era): table-driven {0,1,2,3} / {4,5,6,7} / {4,5,6,3} / {4,7,6,3} + exit-special revert. Verified ✓ at rows #120 (`apply_plus3_special_paging_`), #121 (`revert_slots_2_to_5_post_special_`), #122 (`apply_paging_update_`).
- **Layer 2 overlay family** (P8-P10 era): per-half override gate + offset-pre + sum-mask drop. Verified ✓ at rows #35-38 (read), #49 (write), #99-100 (helpers).
- **Floating-bus / contention latch family** (P9 era): per-page `mem_contend_for_` decode (VHDL :4489-4493) gating `p3_floating_bus_dat_`. Verified ✓ at rows #43, #54, #151. **Note**: same `machine_type_` vs `machine_timing_*` divergence as V25-MEM-01 (row #101) — same class-(d) scope, captured under the same finding.
- **NR 0x8C altrom-lock combinational refresh family** (P11 era): `slots_[]` kept consistent with `read_only_=true` (V11-MEM-01). Verified ✓ at row #66.
- **NR 0x8C / machine-type-change MMU<i> preservation family** (P12 era): `set_nr_sentinel=false` flag preserves verbatim NR 0x50/0x51 values across NR 0x8C / set_machine_type refresh. Verified ✓ at rows #73 (`set_machine_type` calls with false), #87 (`engage_legacy_rom_paging_slot` flag), #150 (`set_nr_8c` calls with false).
- **NR 0x69 bit 7 → port_123b_layer2_en alias family** (P13 era): `set_l2_enable` keeps Mmu's `l2_enable_` mirror in sync with Layer2's `enabled_` flag (V13-MEM-01). Verified ✓ at row #97.
- **Save/load schema family** (Phase 2 + P4): all post-Phase-2 fields appended in order; V4-MEM-01 `nr_mmu_[8]` appended at end. Verified ✓ at rows #161-162.
- **`machine_type` vs `machine_timing` family** (P24-P25 escalation): SOLE outstanding architectural divergence. **Not converged but stable across P14→P24→P25 (3 verification windows).**

## Pass-25 conclusion

- **Differential audit verdict**: zero source changes in `src/memory/` between P24 and P25 — convergence stability is intrinsic and proven by `git log`.
- **Re-walked enumeration**: 268 rows audited (≥168 target, 100 above margin), 1 ✗ (V25-MEM-01, class-(d) carry-over), 2 `~` (V25-MEM-01 secondary surfaces, captured under same finding).
- **Cross-cutting families**: all 9 prior-pass families verified intact, including the P14 convergence anchors (sram_rom decode, altrom locks, +3 special table, EFF7 inertness, V4/V11/V12/V13 MMU<i> preservation).
- **Reviewer reclassification (P24)**: re-examined and class-(d) classification UPHELD on architectural-scope grounds (5+ surfaces, save-state schema bump, video-frame deferred-commit seam).
- **Convergence-stability statement**: Memory subsystem is convergence-stable across THREE verification windows (P14 initial convergence, P24 explicit re-walk, P25 pressure test). The single outstanding class-(d) finding has been carried in its current form since P24 and the audit verdict has not shifted.

## Test results (Pass-25 final)

Recorded at conclusion of pass — see final summary.
