# Pass-14 Memory Subsystem — Independent Review of Zero-Findings Claim

**Branch:** `task2/verify14-memory-reviewer` (off `task2/verify14-memory` at `69eda01`)
**Reviewer:** Pass-14 independent reviewer (different agent from auditor)
**Date:** 2026-05-10
**Audit head:** `69eda01` — doc-only commit, no fix commits (zero findings)

## Verdict

**APPROVE** — the zero-findings claim is honest. After 13 prior passes
(~96 class-(a) + 22 class-(b)/(c) fixes) the memory subsystem is at
defensible class-(a/b/c) convergence within the documented architectural
envelope (half-cycle / `_q` registered signals + machine_type/timing
enum collapse remain class-(d) and out of scope).

**Memory subsystem CONVERGED for class-(a/b/c) and may be skipped in
subsequent passes.** Class-(d) items remain catalogued in the aggregate
report.

## Tests

- Build: Release mode, `cmake --build -j$(nproc)` — success.
- ctest: **38/38 PASS** (incl. mmu, mmu_integration, contention,
  layer2, multiface, divmmc, fuse-z80 unit, etc.).
- FUSE Z80: **1356/1356 PASS** (no skipped, no failed).
- No code changes introduced; no regression possible.

## Per-angle verdict table

| # | Angle | Audit verdict | Reviewer verdict | Notes |
|---|---|---|---|---|
| 1 | Cross-subsystem mirror exhaustive | No gap | **Confirmed** | Walked the FF table; every multi-writer FF identified has both writers updating both shadows. V13-MEM-01 NR $69 b7 → port_123B b1 fan-out validated end-to-end (write_handler at emulator.cpp:2210-2245 calls `mmu_.set_l2_enable`, `set_port_7ffd_bit3`, updates port_ff_reg_, fans renderer + ULA). |
| 2 | Port read-back via Mmu paths | No gap | **Confirmed** | Spot-checked NR $12/$13/$8C/$8E/$8F/$69/$50-$57/$08 reads against VHDL :5894/5930-5933/6075-6082/6095-6096/6155-6162. All compositions match. |
| 3 | `*_q` registered signals | Class-(d) only | **Confirmed** | Only memory-side `_q` is `port_7ffd_dat <= port_7ffd_reg` on falling-edge (VHDL:3676-3681). Bank composition (:3763-3766) uses the live `port_7ffd_reg`; only the `port_7ffd_shadow` derived signal (:3768) uses the delayed version. C++ collapses both into `port_7ffd_` — half-cycle granularity, class-(d). No new class-(a/b/c). |
| 4 | Save/load round-trips | No gap | **Confirmed** | Walked `Mmu::save_state` (mmu.cpp:811-881) — every persisted field listed in audit § 4 is written and re-read. Emulator-level rebuild (`contention_.rebuild_for_type` + reset of `cpu_speed`/`contention_disable`/`port_7ffd_io_en`) re-derives the dynamic gates. V12-MEM-02 discriminative test guards this. |
| 5 | Default values on cold/warm/config-mode reset | No gap | **Confirmed** | mmu.cpp:84-166 reset path matches VHDL :4611-4618 (slots 0xFF/FF/0A/0B/04/05/00/01) plus the special non-zeroed cases: nr_8c lo→hi nibble copy (VHDL:2253-2256 = mmu.cpp:104-107), nr_8f preserved (VHDL:3787-3794 — no reset clause = mmu.cpp:97-101 comment), bootrom re-enable gated on config_mode (VHDL:5109-5111 = mmu.cpp:131-149). |
| 6 | NR $03 machine_type transitions | No gap | **Confirmed** | emulator.cpp:1949-2095 NR $03 write handler: gates timing on bit7+!dt_lock+!bit3 (VHDL:5124-5133), gates type on config_mode (VHDL:5137-5145), unconditionally clears bootrom_en (VHDL:5122). On commit: `mmu_.set_machine_type` (rebuilds legacy-ROM slot 0/1 only, preserves explicit RAM mappings — Verify7), `divmmc_.set_rom3_active(mmu_.sram_rom3())` (Verify7), `contention_.rebuild_for_type` (Verify9). |
| 7 | Multi-overlay precedence | No gap | **Confirmed** | mmu.h read()/write() priority cascade matches VHDL :2937-2945 / :3084-3131: BootROM > MF > DivMMC > Layer2 > Alt-ROM > config-mode SRAM > normal MMU. Per-half L2 gate (l2_overlay_active_for) matches VHDL:3043/:3050/:3057 (low-half always on in non-MF cases) + :3065 (high half: seg=11 + addr<0xC000) exactly. |
| 8 | Layer 2 base/segment combinations | No gap | **Confirmed** | mmu.h:1124-1146 l2_overlay_active_for + l2_offset_pre_for match VHDL :2966 / :3037-3066. Layer2 active_bank / shadow_bank both 7-bit masked (Layer2.h:38, mmu.h:1027/1036). NR $12/$13 read pulls 7-bit value via `& 0x7F` (emulator.cpp:744). VHDL :2971 layer2_A21_A13(8) gate (sum&0x70==0x70 → SRAM inactive) honored on read (mmu.h:283-298) AND write (mmu.h:429-431) — Verify10 fix. |
| 9 | MMU+1 wraparound at slot 7 → slot 0 | Moot | **Confirmed** | grep of zxnext.vhd shows no MMU+1 logic exists. C++ uses `addr >> 13` indexing 0..7 strictly. Audit's "no wraparound issue" is correct because there's no wraparound to handle. |
| 10 | EFF7(3) + machine_type interaction | No gap | **Confirmed** | mmu.cpp:416-418 EFF7(3)=1 forces `set_page(0,0x00)+set_page(1,0x01)`. NR $50/$51,$FF dispatch routes to engage_legacy_rom_paging_slot — does NOT honour EFF7(3) override (correct per VHDL :3813 — `nr_mmu_we` does NOT trigger port_memory_change_dly). Verify4 fix. Re-verified by tracing port_eff7_wr → apply_paging_update_ → apply_legacy_rom_slots_ chain. |
| 11 | +3 special paging table | No gap | **Confirmed** | mmu.cpp:465-503 apply_plus3_special_paging_ table matches VHDL :4625-4632 bit-reconstruction for all 4 (B,A) combinations. revert_slots_2_to_5_post_special_ at :488-491 matches VHDL :4655-4670 default revert (slots 2-5 → 0x0A/0x0B/0x04/0x05). |
| 12 | NR $69 multi-fan-out | Confirmed by audit | **Confirmed** | All 4 fan-out legs verified: layer2_.set_enabled (renderer), mmu_.set_l2_enable (port 0x123B readback — V13-MEM-01), mmu_.set_port_7ffd_bit3 + renderer_.ula().set_shadow_screen_en (shadow), port_ff_reg_ + renderer_.ula().set_screen_mode (Timex screen mode). VHDL refs :3617-3625 / :3658-3660 / :3924-3925. Discriminative test (mmu_integration_test.cpp:496-613) covers V13-MEM-01-A through V13-MEM-01-E with sweep verification. |
| 13 | NR $8E lock bypass + MMU6/7 suppression | No gap | **Confirmed** | mmu.cpp:695-763 write_nr_8e: bypasses lock (no port_7ffd_locked check on the bit-3 path), correctly suppresses MMU6/7 rebuild when bit3=0 by routing through `apply_legacy_rom_slots_()` instead of `apply_paging_update_()`. Matches VHDL :3662-3672 / :3814 / :4677. Special-mode entry/exit transitions (port_1ffd_special / `port_1ffd_special_old_`) handled at mmu.cpp:750-762. |

## Spot-checks (5+)

### Spot-check 1 — V13-MEM-01 fan-out integrity (NR $69 b7 → port_123B)

**VHDL oracle:** zxnext.vhd:3924-3925
```vhdl
elsif nr_69_we = '1' then
   port_123b_layer2_en <= nr_wr_dat(7);
```
**C++:** `emulator.cpp:2222` calls `mmu_.set_l2_enable((v & 0x80) != 0)`.
The same FF (`port_123b_layer2_en`) is also written by port 0x123B at
:3916. Both writers in C++ now update both Mmu's `l2_enable_` shadow
AND Layer2's `enabled_` shadow (port 0x123B path: emulator.cpp:2701-2715,
NR $69 path: emulator.cpp:2210-2245). Discriminative test
mmu_integration_test.cpp:496-613 confirms post-fix behavior end-to-end.

### Spot-check 2 — Port 0x7FFD lock bypass on NR $8E

**VHDL oracle:** zxnext.vhd:3650 vs :3662
```vhdl
elsif port_7ffd_wr = '1' and port_7ffd_locked = '0' then  -- gated
   port_7ffd_reg <= cpu_do;
elsif nr_08_we = '1' and nr_wr_dat(7) = '1' then
   port_7ffd_reg(5) <= '0';
elsif nr_69_we = '1' then            -- ungated
   port_7ffd_reg(3) <= nr_wr_dat(6);
elsif nr_8e_we = '1' then            -- ungated
   ...
```
**C++:** `mmu.cpp:695` `write_nr_8e` does not check `paging_locked_`,
correctly bypassing the lock per VHDL elsif chain. The port-7FFD path
(via `map_128k_bank`) does check the lock (`effective_paging_locked()`
at mmu.h:589-591). NR $69 bit-3 fan-out (`set_port_7ffd_bit3`) also
bypasses the lock (mmu.cpp setter just clears/sets bit 3 verbatim).
Matches VHDL.

### Spot-check 3 — NR $50-$57 MMU<i> verbatim store + read-back

**VHDL oracle:** zxnext.vhd:4686-4699 (write) + :6075-6082 (read).
On `nr_mmu_we='1'`, `MMU<i> <= nr_wr_dat` verbatim. NR-port read returns
the live MMU<i>.

**C++:** `emulator.cpp:1592-1665` dispatcher: NR $50/$51 with v=0xFF
routes via `engage_legacy_rom_paging_slot` (sentinel + sram_rom-derived
page); NR $52-$57 with v=0xFF stores 0xFF directly via `set_page` (slot
becomes inactive); other values store verbatim. The NR read handler at
:1661-1664 returns `mmu_.get_page(i)` which surfaces `nr_mmu_[i]` —
preserving verbatim 0xE0..0xFE writes (Verify11 fix). Confirmed.

### Spot-check 4 — Layer 2 high-half segment gate

**VHDL oracle:** zxnext.vhd:3065
```vhdl
sram_pre_override <= '0' & (((not cpu_a(15)) or (not cpu_a(14))) and
                            port_123b_layer2_map_segment(1) and
                            port_123b_layer2_map_segment(0)) & '0';
```
For high half (cpu_a(15:14) ∈ {01, 10, 11}): L2 enabled iff
`((not a15) OR (not a14)) AND seg=11`. At addr ≥ 0xC000 (cpu_a(15:14)=11),
the first term is 0 → L2 disabled. At 0x4000-0xBFFF: L2 enabled iff seg=11.

**C++:** `mmu.h:1124-1135` `l2_overlay_active_for`:
```cpp
if (addr < 0x4000)  return /* low half: always TRUE in non-MF case */;
if (addr >= 0xC000) return false;
return l2_segment_raw_ == 0x03;
```
For low half VHDL :3043/:3050/:3057 set `override(1)=1` independently of
seg, matching the C++ unconditional TRUE. Confirmed.

### Spot-check 5 — Save-state round-trip persists `nr_mmu_[]` verbatim

**VHDL oracle:** zxnext.vhd:4686-4699 stores nr_wr_dat verbatim into
MMU<i>; the NR-port read at :6075-6082 surfaces this verbatim value.
For high-page values (0xE0..0xFE) the SRAM arbiter at :3037-3057 still
falls into the legacy-ROM branch (mmu_A21_A13(8)='1' gate), but the NR
read still returns the verbatim stored value.

**C++:** `mmu.cpp:880` writes `nr_mmu_[8]` verbatim into the save
stream. Pre-Verify4 the field was reconstructed from
`(read_only_[i] ? 0xFF : slots_[i])`, losing 0xE0..0xFE values. Verify4
fix persists the array directly. Confirmed.

### Spot-check 6 — DMA respects the same paging table

DMA's memory callbacks: `dma_.read_memory = [this](uint16_t addr) -> uint8_t { return mmu_.read(addr); }`
(emulator.cpp:4184-4185). Same mmu_.read/write entry points as CPU, so
DMA accesses honor MF / DivMMC / Layer 2 / altrom / config-mode
overlays identically. Copper does not access main memory directly (it
issues NEXTREG writes through `nextreg.write` — no separate paging
path). No DMA/Copper paging gap.

## Missed-findings section

**None.** I attempted the following angles the audit did NOT explicitly
list, and found no class-(a/b/c) issue:

- **DMA + CPU paging consistency** — DMA uses `mmu_.read`/`write`,
  same path as CPU. No divergence.
- **Copper memory access** — Copper only writes NEXTREGs via
  `nextreg.write`; no main-memory bus.
- **NR $03 read returning palette_sub_idx** — `nr_palette_sub_idx`
  (VHDL :5894 bit 7 of NR $03 read) is NOT modeled in C++. This is
  a class-(c)-or-similar gap, but it belongs to the **palette subsystem
  (NR $44 sub-index toggle)**, NOT the memory subsystem. Out of scope
  for memory convergence. Documented in emulator.cpp:2106-2108 as a
  known C++ gap.
- **NR $03 bit 7 (machine timing) handling** — gates and dispatch match
  VHDL :5124-5133. C++ correctly only commits when bit7=1, !dt_lock, !bit3.
- **NR $14 / $15 / $16** — these are video registers (transparent RGB,
  layer priority, etc.), not memory subsystem.
- **Pentagon-1024 lock-bypass via EFF7(2)** — `effective_paging_locked()`
  at mmu.h:589-591 implements `paging_locked_ && !pentagon_1024_en()`
  matching VHDL :3769 (with profi=0). Correct.
- **Alt-ROM read vs write polarity** — read uses
  `nr_8c_altrom_en && !altrom_rw && !config_mode && read_only` (mmu.h:320),
  write uses `nr_8c_altrom_en && altrom_rw && !config_mode && read_only`
  (mmu.h:448). Matches VHDL :3078 (rdonly=1 ⟹ writes go to normal path,
  reads to altrom; rdonly=0 ⟹ writes to altrom, reads to normal).
- **port_dffd_reg width and bit 4** — stored as 5 bits (`& 0x1F`,
  mmu.cpp:646). Bit 4 is profi-only; profi=0 globally so bit 4 is dead
  but stored for save/load parity. No gap.
- **NR $8E with bit 3=0 + EFF7(3)=1** — `apply_legacy_rom_slots_()` at
  mmu.cpp:410-438 honors EFF7(3) override on the rebuild path (set_page(0,0x00)
  + set_page(1,0x01)). Matches VHDL :4636-4644 (port_memory_change_dly
  fires on nr_8e_we per :3813, so the EFF7 override branch executes).
- **NR $8C lo→hi nibble copy on reset** — mmu.cpp:104-107 `(lo << 4) | lo`
  matches VHDL :2253-2256 `nr_8c_altrom(7:4) <= nr_8c_altrom(3:0)`.

The audit was thorough and honest; the convergence claim holds.

## Conclusion

The Pass-14 audit's zero-findings claim is **independently confirmed**.

After 14 passes (10 audit + 4 verify) plus a retroactive test-coverage
wave, the memory subsystem is at honest convergence for class-(a/b/c)
findings within the documented architectural envelope. The remaining
class-(d) items (half-cycle / `_q` registered signals; MachineType vs.
nr_03_machine_timing enum collapse) are listed in the aggregate report
and require explicit user authorization for architectural changes —
they are not appropriate for class-(a/b/c) iteration.

**Memory subsystem CONVERGED. Skip in subsequent passes.**

| Test category | Result |
|---|---|
| ctest (unit + integration) | 38/38 PASS |
| FUSE Z80 opcode suite | 1356/1356 PASS |
| Build (Release) | success |

