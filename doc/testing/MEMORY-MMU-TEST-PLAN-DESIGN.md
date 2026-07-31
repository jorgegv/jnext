# Memory — MMU & RAM Compliance Test Suite

VHDL-derived compliance test plan for the Memory/MMU subsystem of the JNEXT
ZX Spectrum Next emulator. All expected behaviour is derived exclusively from
the VHDL source (`zxnext.vhd`), not from the C++ implementation.

## Purpose

The MMU subsystem translates Z80 addresses (0x0000-0xFFFF) into physical SRAM
addresses (22-bit, up to 4 MB) through an 8-slot paging scheme. It is the
foundation for all memory access in the emulator. This test suite validates:

- 8K MMU slot assignment via NextREGs 0x50-0x57
- Legacy 128K/+3 paging via ports 0x7FFD, 0x1FFD, 0xDFFD, 0xEFF7
- ROM selection and read-only enforcement
- Config mode and ROMRAM bank
- Alternate ROM (NR 0x8C)
- Layer 2 memory mapping overlay
- Bank 5/7 special handling (dual-port VRAM)
- Memory contention rules
- Address-to-SRAM translation formula
- NR 0x8E unified paging register
- Pentagon/Profi mapping modes (NR 0x8F)
- +3 special paging modes

## Current status

Rewrite in Phase 2 per-row idiom merged on main 2026-04-15 (`task1-wave1-mmu`).

Task 8 Wave 1 (`task8-t1-mmu`, 2026-04-28) closed 5 skips and re-homed 1:
- **BOOT-OVL-01 / BOOT-OVL-02 (G140)**: VHDL-faithful boot ROM mirror.
  `Mmu::read()` now gates `addr < 0x4000` (VHDL zxnext.vhd:1856,
  cpu_a(15:14)='00') and indexes `addr & 0x1FFF` (VHDL:3199-3204,
  cpu_a(12:0) — 13-bit, 8 KB span — so upper 8 KB mirrors lower 8 KB).
- **BOOT-OVL-03 (G157)**: Wrong-sized boot ROM blob. `Mmu::set_boot_rom`
  now materialises an 8 KB internal buffer, zero-padding short blobs and
  truncating long ones, with a warn-level diagnostic. Read path is
  always real-hardware-faithful regardless of caller size.
- **BOOT-NEX-01 / BOOT-NEX-02 (G155)**: NEX `ram_required` validation.
  Added inline static helpers `NexLoader::ram_required_kb` /
  `ram_required_fits` (header-resident so unit tests link without
  jnext_core). `NexLoader::apply` aborts with a clear error before any
  bank load when required > installed RAM.
- **EF7-06 (G143) RE-HOME**: port_eff7_io_en gate added on the EFF7
  port handler in `src/core/emulator.cpp` (mirrors the existing NR 0x82
  b2 gate on DFFD). Original 2026-04-28 fix gated against NR 0x84 b2;
  Tier A SKIP-reduction (2026-05-04) corrected this to NR 0x85 b2 per
  VHDL `zxnext.vhd:2392,2441` — `internal_port_enable(26)` sits in the
  nr_85 range. Re-homed live at `test/mmu/mmu_integration_test.cpp`
  (MMU-EF7-IO-EN-00..02).
- mmu_test counts: 41 → 36 skips (5 closures); 137 → 142 pass; 0 fails.

Measured on main 2026-04-21 post-Task-3 MMU Wave 1 merges:

- **145 plan rows total** (N8E-05 split into 05a/05b + CON-12 split into 12a/12b + L2M-02 split into 02a/02b in test — aggregated by matrix script per SUBLETTERS rule).
- **mmu_test binary**: **148 pass, 0 fail, 0 skip** — FULLY GREEN.
- Task-3 Wave 1 (2026-04-21) closed the last 13 skips via three parallel branches:
  - **Feature X** `task3-mmu/floating-bus` (commit 19ca74e): floating-bus gate on RAM slots when page ≥ 0xE0. In `Mmu::rebuild_ptr`, `slot >= 2 && page >= 0xE0` → `read_ptr_[slot]=write_ptr_[slot]=nullptr`, the existing null-pointer handling returns 0xFF on read and drops writes per VHDL zxnext.vhd:3060-3061 (`sram_pre_active='0'` on inactive RAM). Un-skipped MMU-12, ADR-09, ADR-10.
  - **Feature Y** `task3-mmu/altrom-arb` (commits 073930b + 7e54734): NR 0x8C altrom SRAM arbiter — read + write paths. Read takes altrom when `altrom_en=1 AND altrom_rw=0` (VHDL:3078, read-only altrom); write takes altrom when `altrom_en AND altrom_rw` (write-over). Machine-type-aware `alt_128_n` selector per VHDL:2981-3008; page index per VHDL:3117 maps to SRAM pages 12..15. Un-skipped ROM-09, ALT-08, PRI-06.
  - **Feature Z** `task3-mmu/int-rehome` (commits 3cadc60 + 5dc3543): re-home 6 bare-class skips whose observables live on other subsystems. P7F-11 → ULA tests (S15.02/04); L2M-05/06 → 4 new check()s in nextreg_integration_test.cpp; PRI-01/02/04 → tracking skips in divmmc_test.cpp. Also fixed a latent NR 0x12/0x13 readback bug surfaced by the critic: NextReg::write stores raw regs_[] pre-dispatch, so reads returned the unmasked byte instead of the VHDL-spec 7-bit masked value (zxnext.vhd:5930-5931). Installed read_handlers on NR 0x12/0x13 pulling from Layer2 directly.
- **P1F-07 (+3 disk motor)**: converted to WONT comment on 2026-04-21 (commit 3dd892a) — refinement of feedback_unobservable_audit_rule.md category G, explicit decision record (jnext does not model the +3 FDC; NextZXOS / SD / NEX loaders cover relevant software).
- (Historical earlier work preserved below for context; no new skips remaining.)
- Phase 2 D2 (`fix/mmu-branch-d2`) un-skipped L2M-02 (Layer 2 read-over via port 0x123B bit 2 `layer2_map_rd_en`). Renamed `Mmu::set_l2_write_port` → `Mmu::set_l2_port` (atomic latch of both read-enable bit 2 and write-enable bit 0 per VHDL zxnext.vhd:3918). Added L2 read block to `Mmu::read()` — priority after DivMMC, before config-mode/MMU (VHDL:3100-3107); page math byte-identical to write path (VHDL:2966-2971). Added `l2_read_enable_` state (save/load round-trip + reset-clear parity with write side). Split L2M-02 into 02a (positive: read-enable=1 returns L2-bank byte) + 02b (discriminative: read-enable=0 returns MMU byte — catches stuck-enabled bug). Refreshed `divmmc_test.cpp:R3-03` skip-reason comment (stale L2-write-only claim). VHDL citations inline in test rows.
- Phase 2 D1 (`fix/mmu-branch-d1`) un-skipped 12 rows: CON-01..12 (memory-contention gate per VHDL zxnext.vhd:4481 i_contention_en AND :4489-4493 mem_contend). Added four VHDL-spec inputs to `ContentionModel`: `mem_active_page`, `cpu_speed` (2 bits), `pentagon_timing`, `contention_disable`; plus combined `is_contended_access()` gate. NR 0x07/0x08 write handlers and `Emulator::init()` now mirror into the model (runtime tick-loop integration explicitly deferred — known regression risk). Added discriminative CON-12b (48K + `set_pentagon_timing(true)`) to catch hypothetical `build()` bugs where Pentagon machine-type fall-through alone would mask a broken gate. Fixed plan-doc CON-06 arithmetic typo (page 0x04 not 0x02 — bit 1 of 0x02 is 1, would be contended). VHDL citations inline in test rows.
- Phase 2 B (`fix/mmu-branch-b`) un-skipped 13 rows: N8E-01..06 (unified paging write + read-back, NR 0x8E bypasses paging lock, bit 3=1 clears DFFD(3)), N8F-01..05 (mapping modes incl. Pentagon-512/1024 bank composition, EFF7(2) gates P1024 lock override, bank(6) forced 0 in Pentagon modes), LCK-05 (Pentagon-1024 overrides lock via `effective_paging_locked()`), LCK-07 (NR 0x8E bypasses lock). Added `nr_8f_mode_` storage + `compose_bank_` helper replacing the Branch-A `apply_legacy_paging_` bank-math. VHDL citations: zxnext.vhd:3662-3734 (NR 0x8E write decode), 3763-3769 (Pentagon bank composition), 3787-3794 (NR 0x8F storage — no reset process; declaration-level default only per VHDL:888), 3801 (pentagon_1024_en final composition), 6159 (NR 0x8E read-back formula).
- **NR 0x8F reset**: does NOT clear `nr_8f_mode_` on either hard OR soft reset. VHDL has no reset process for `nr_8f_mapping_mode`; FPGA configuration-time default is the only zero event. Deviates from Branch C convention but matches VHDL exactly.
- Phase 2 C0 (commit `354fa14`) landed NR 0x08 bit 7 paging unlock — un-skipped P7F-14 and LCK-04.
- Phase 1a re-triage: un-skipped BNK-01..04 (dual-port bypass outcome tests). MMU-12, ADR-09, ADR-10 were initially un-skipped but REVERTED to skip() after independent critic review flagged SX-02 anti-pattern (tests encoded JNEXT's `to_sram_page` truncation as the oracle instead of VHDL's `sram_pre_active=0` floating-bus semantics per zxnext.vhd:3060-3061).
- Phase 2 C (`fix/mmu-branch-c`) un-skipped 16 rows: ROM-01..07 (machine-type / sram_rom accessor per zxnext.vhd:2981-3008), ALT-01..07 + ALT-09 (NR 0x8C altrom register storage + decoded accessors), plus a bonus un-skip for RW-02 in the integration tier via the NR 0x08 read handler (bit 7 = NOT paging-lock, bit 6 = contention-disable).
- Phase 2 C also added `Mmu::reset(bool hard)` overload: VHDL-faithful soft reset now preserves `paging_locked_`, `contention_disabled_`, and NR 0x8C bits 3:0 across RESET_SOFT (all three previously cleared unconditionally — a pre-existing divergence from C0 that Branch C took care of while adding the NR 0x08 bit 6 + NR 0x8C state). VHDL citations: zxnext.vhd:1730 (hard-reset signal), 2253-2256 (NR 0x8C nibble copy), 3646-3648 (port_7ffd_reg clear), 4930-4935 (contention_disable clear).
- Phase 2 A (`fix/mmu-branch-a`) un-skipped 12 rows (DFF-01..07, LCK-03, EF7-01..04) and added 2 new rows (DFF-08, EF7-05) covering soft-reset preservation — all 14 passing. Implemented `Mmu::write_port_dffd` (lock-gated per VHDL:3691) + `Mmu::write_port_eff7` (ungated per VHDL:3781); EFF7 bit 3 re-maps slots 0/1 to RAM pages 0x00/0x01 per VHDL:4636-4644; DFFD bank composition `port_7ffd(2:0) | (port_dffd(4:0)<<3)` per VHDL:3763-3766. Soft reset preserves both registers (VHDL:3687, :3777) AND their downstream page-map effects (DFFD→MMU6/7, EFF7→MMU0/1) via a post-seed `apply_legacy_paging_()` call in `Mmu::reset(false)` — emulator must re-assert because our MMU state is imperative where VHDL is combinational.
- Phase 2 A.1 (`fix/palette-pal-bugs`) resolved 3 palette FAILs surfaced by Phase 1b's integration rewrite (PAL-01/03/06). Root cause: NR 0x41 read handler returned stale `regs_[0x41]`; write-side was already correct. Fix added `PaletteManager::read_8bit(target, idx)` + wired NR 0x41 reader in `src/core/emulator.cpp:220`. VHDL: zxnext.vhd:6038-6039. Aggregate unit went 2721/3/527 → 2724/0/527.
- **Previously-listed RST-01/RST-02 failures**: already fixed by earlier reset-seed work — all eight RST rows pass (MMU0/MMU1 seed to the 0xFF ROM sentinel per VHDL zxnext.vhd:4611-4618).
- **Remaining 13 skips blocked by** 3 DivMmc-overlay rows (PRI-01/02/04) destined for integration tier, 2 altrom SRAM-arbiter overrides (ALT-08, ROM-09 — need full sram_pre_rdonly wiring), NR 0x12/0x13 shadow L2M-05/06 (integration tier), Tilemap NR 0x1B read cycling (Phase 2 E), plus 5 additional integration-tier rows owned by cross-subsystem fixtures. All pure MMU-surface work on the VHDL paging/overlay axis is now complete.
- **VHDL-deviation backlog from Phase 1a critic:** MMU-12 / ADR-09 / ADR-10 observable: page ≥0xE0 on a RAM slot. VHDL inactivates; JNEXT wraps via `to_sram_page` and reads ROM-in-SRAM page 0 instead. Real deviation, no known software impact today. Fix: either gate RAM slots on mmu_A21_A13(8) or document the simplification.
- **Pre-existing soft-reset divergence (informational):** `nr_04_romram_bank_` is cleared unconditionally on every Mmu reset; VHDL (zxnext.vhd:1104) initialises the signal with no reset process — holds across both domains. Benign for current boot path (firmware rewrites NR 0x04 before each config_mode entry). Flagged as backlog.

## VHDL Architecture Summary

### Physical Memory Map (SRAM)

From `zxnext.vhd` lines 2920-2931:

| Address Range       | Size  | Content               | A20:A16  |
|---------------------|-------|-----------------------|----------|
| 0x000000 - 0x00FFFF | 64K   | ZX Spectrum ROM       | 00000    |
| 0x010000 - 0x011FFF |  8K   | DivMMC ROM            | 00001,000|
| 0x012000 - 0x013FFF |  8K   | unused                | 00001,001|
| 0x014000 - 0x017FFF | 16K   | Multiface ROM,RAM     | 00001,01 |
| 0x018000 - 0x01BFFF | 16K   | Alt ROM0 128K         | 00001,10 |
| 0x01C000 - 0x01FFFF | 16K   | Alt ROM1 48K          | 00001,11 |
| 0x020000 - 0x03FFFF | 128K  | DivMMC RAM            | 00010    |
| 0x040000 - 0x05FFFF | 128K  | ZX Spectrum RAM       | 00100    |
| 0x060000 - 0x07FFFF | 128K  | Extra RAM             |          |
| 0x080000 - 0x0FFFFF | 512K  | 1st Extra IC RAM      |          |
| 0x100000 - 0x17FFFF | 512K  | 2nd Extra IC RAM      |          |
| 0x180000 - 0x1FFFFF | 512K  | 3rd Extra IC RAM      |          |

### MMU Registers (NR 0x50-0x57)

Eight 8-bit registers, one per 8K slot:

| Slot | NR   | Address Range       | Reset Value |
|------|------|---------------------|-------------|
| 0    | 0x50 | 0x0000 - 0x1FFF    | 0xFF        |
| 1    | 0x51 | 0x2000 - 0x3FFF    | 0xFF        |
| 2    | 0x52 | 0x4000 - 0x5FFF    | 0x0A        |
| 3    | 0x53 | 0x6000 - 0x7FFF    | 0x0B        |
| 4    | 0x54 | 0x8000 - 0x9FFF    | 0x04        |
| 5    | 0x55 | 0xA000 - 0xBFFF    | 0x05        |
| 6    | 0x56 | 0xC000 - 0xDFFF    | 0x00        |
| 7    | 0x57 | 0xE000 - 0xFFFF    | 0x01        |

**Page value 0xFF** = ROM. When a slot has value 0xFF, the MMU address formula
produces `mmu_A21_A13(8) = '1'`, which causes the ROM path to be selected
instead of RAM.

### Address Translation Formula

```
mmu_A21_A13 = (0x01 + mem_active_page(7 downto 5)) & mem_active_page(4 downto 0)
```

This maps page N to physical SRAM address `(0x01 + N/32) * 8K + (N mod 32) * 8K`,
effectively: `sram_base = (N + 32) * 8192` for page N. If `mmu_A21_A13(8)` is
set (i.e., page >= 224), the address overflows and the ROM/config path is taken.

The full SRAM address is: `sram_addr = sram_A21_A13 & cpu_a(12 downto 0)`.

### Memory Decode Priority

**0-16K region** (cpu_a(15:14) = "00"):
1. Boot ROM
2. Multiface
3. DivMMC
4. Layer 2 mapping
5. MMU
6. Config mode (NR 0x04 ROMRAM bank)
7. ROMCS expansion bus
8. ROM

**16K-48K region** (cpu_a(15:14) = "01" or "10"):
1. Layer 2 mapping
2. MMU

**48K-64K region** (cpu_a(15:14) = "11"):
1. MMU only

### ROM Selection

ROM page is determined by `sram_rom` (2-bit) based on machine type:

**48K mode**: Always ROM 0 (`sram_rom = "00"`), unless altrom lock overrides.

**+3 mode**: ROM selected by `port_1ffd_rom = port_1ffd_reg(2) & port_7ffd_reg(4)`:
- 00 = ROM 0 (128K editor)
- 01 = ROM 1 (128K syntax)
- 10 = ROM 2 (+3 DOS)
- 11 = ROM 3 (48K BASIC)

**128K mode**: ROM selected by `port_1ffd_rom(0) = port_7ffd_reg(4)`:
- 0 = ROM 0 (128K editor)
- 1 = ROM 1 (48K BASIC)

Altrom lock (NR 0x8C bits 5:4) overrides the ROM selection in all modes.

### Port 0x7FFD — 128K Paging

```
Bit 0-2: RAM bank for slot 6/7 (bank bits 2:0)
Bit 3:   Shadow screen select (bank 5 or 7 for ULA)
Bit 4:   ROM select bit 0
Bit 5:   Lock bit (when set, ports 7FFD/1FFD/DFFD are locked)
```

On write, MMU6/MMU7 are set to `port_7ffd_bank & '0'` / `port_7ffd_bank & '1'`.

`port_7ffd_bank` is a 7-bit value composed from multiple registers:
```
port_7ffd_bank(2:0) = port_7ffd_reg(2:0)
port_7ffd_bank(4:3) = port_7ffd_reg(7:6)  [Pentagon mode]
                     = port_dffd_reg(1:0)  [otherwise]
port_7ffd_bank(5)   = port_dffd_reg(2)    [normal]
                     = pentagon_1024_en AND port_7ffd_reg(5)  [Pentagon 1024]
port_7ffd_bank(6)   = 0                   [Pentagon or Profi]
                     = port_dffd_reg(3)    [otherwise]
```

Lock: `port_7ffd_locked = 0` when Pentagon-1024 enabled or Profi mode with
dffd(4)=1; otherwise `port_7ffd_locked = port_7ffd_reg(5)`.

### Port 0xDFFD — Extra RAM Bits

```
Bit 0-4: Extra paging bits (port_dffd_reg)
Bit 6:   Profi bank 6 enable (port_dffd_reg_6)
```

Write requires `port_7ffd_locked = 0` OR Profi mode enabled.

### Port 0x1FFD — +3 Paging

```
Bit 0: Special mode enable (port_1ffd_special)
Bit 1: port_1ffd_reg(1) — used in special mode RAM config
Bit 2: port_1ffd_reg(2) — ROM bank select high bit
Bit 3: Disk motor (separate handling)
```

Write requires `port_7ffd_locked = 0`.

### +3 Special Paging Mode

When `port_1ffd_special = 1`, all 8 MMU slots are set to all-RAM
configurations. Let R21 = `port_1ffd_reg(2) or port_1ffd_reg(1)`,
R21_and = `port_1ffd_reg(2) and port_1ffd_reg(1)`,
R1not2 = `not(port_1ffd_reg(2)) and port_1ffd_reg(1)`:

| Config bits (2:1) | MMU0 | MMU1 | MMU2 | MMU3 | MMU4 | MMU5 | MMU6 | MMU7 |
|-------------------|------|------|------|------|------|------|------|------|
| 00 (bits=00)      | 0x00 | 0x01 | 0x02 | 0x03 | 0x04 | 0x05 | 0x06 | 0x07 |
| 01 (bits=01)      | 0x08 | 0x09 | 0x0A | 0x0B | 0x0C | 0x0D | 0x0E | 0x0F |
| 10 (bits=10)      | 0x08 | 0x09 | 0x0A | 0x0B | 0x0C | 0x0D | 0x06 | 0x07 |
| 11 (bits=11)      | 0x08 | 0x09 | 0x0E | 0x0F | 0x0C | 0x0D | 0x06 | 0x07 |

Derivation from VHDL (line 4625-4632):
- MMU0 = `0x0` & R21 & `00` & `0` — e.g., for bits=01: 0x08
- MMU1 = `0x0` & R21 & `00` & `1` — e.g., for bits=01: 0x09
- MMU2 = `0x0` & R21 & R21_and & `1` & `0`
- MMU3 = `0x0` & R21 & R21_and & `1` & `1`
- MMU4 = `0x0` & R21 & `10` & `0`
- MMU5 = `0x0` & R21 & `10` & `1`
- MMU6 = `0x0` & R1not2 & `11` & `0`
- MMU7 = `0x0` & R1not2 & `11` & `1`

### Port 0xEFF7

```
Bit 2: port_eff7_reg_2 — disables Pentagon 1024 mode when set
Bit 3: port_eff7_reg_3 — forces ROM pages 0,1 into MMU0/MMU1 (RAM at 0x0000)
```

When `port_eff7_reg_3 = 1` (and not in special mode), MMU0/MMU1 are set to
0x00/0x01 (RAM pages) instead of 0xFF (ROM).

### NR 0x8E — Unified Paging Register

Single-write register that simultaneously updates ports 7FFD, DFFD, and 1FFD:

```
Bit 7:   port_dffd_reg(0)  — extra RAM bit 0
Bit 6:4: port_7ffd_reg(2:0) — bank select (only if bit 3 = 1)
Bit 3:   Enable bank select (when 1, bits 7,6:4 update bank; when 0, bit 0 updates ROM)
Bit 2:   Special mode enable → port_1ffd_reg(0)
         When bit 2 = 0 AND bit 3 = 1: ROM select is NOT changed
         When bit 2 = 0: port_7ffd_reg(4) = bit 0
Bit 1:   port_1ffd_reg(2) — +3 ROM high / special config
Bit 0:   port_1ffd_reg(1) — special config
         When bit 2 = 0 AND bit 3 = 0: port_7ffd_reg(4) = bit 0 (ROM select)
```

Read-back at NR 0x8E returns:
`port_dffd_reg(0) & port_7ffd_reg(2:0) & '1' & port_1ffd_reg(0) & port_1ffd_reg(2) & ((port_7ffd_reg(4) AND NOT port_1ffd_reg(0)) OR (port_1ffd_reg(1) AND port_1ffd_reg(0)))`

### NR 0x8F — Mapping Mode

```
Bits 1:0: Mapping mode
  00 = Standard ZX Next (default)
  01 = Profi mode (DISABLED in VHDL — hardcoded to 0)
  10 = Pentagon 512K
  11 = Pentagon 1024K
```

Note: Profi mode is commented out in VHDL (`nr_8f_mapping_mode_profi <= '0'`).

Pentagon mode changes how `port_7ffd_bank` is composed (bits 7:6 of 7FFD used
instead of DFFD bits 1:0).

Pentagon-1024 mode: `port_7ffd_locked` is forced to 0, allowing unlimited bank
switching. Enabled when mode=11 AND `port_eff7_reg_2 = 0`.

### Config Mode (NR 0x03/0x04)

When `nr_03_config_mode = 1` (set during boot/firmware), the 0-16K region maps
to the ROMRAM bank specified by NR 0x04 instead of ROM:
```
sram_pre_A21_A13 = nr_04_romram_bank & cpu_a(13)
```

NR 0x04 value depends on board issue:
- Issue 2-4: `'0' & nr_wr_dat(6:0)` (7-bit, max 128 banks)
- Issue 5+: `nr_wr_dat` (8-bit, max 256 banks)

### Alternate ROM (NR 0x8C)

```
Bit 7: Enable alternate ROM (nr_8c_altrom_en)
Bit 6: Read/Write enable (nr_8c_altrom_rw) — when 1, alt ROM is writable
Bit 5: Lock ROM1 selection (nr_8c_altrom_lock_rom1)
Bit 4: Lock ROM0 selection (nr_8c_altrom_lock_rom0)
Bit 3:0: Preserved across reset (copied to bits 7:4 on reset)
```

When altrom is enabled AND the access is to ROM space:
- Address: `"0000011" & sram_pre_alt_128_n & cpu_a(13:0)`
- Alt ROM 0 (128K): SRAM 0x018000-0x01BFFF
- Alt ROM 1 (48K): SRAM 0x01C000-0x01FFFF

The `sram_alt_128_n` signal selects which alt ROM based on the current ROM page
and lock bits. When altrom_rw=0, ROM space is read-only even with alt ROM enabled.

### Bank 5 and Bank 7 Special Handling

Pages 0x0A and 0x0B (bank 5) and page 0x0E (bank 7) are flagged as special
because they are implemented as dual-port BRAM on the FPGA, shared with the
ULA/tilemap video hardware:

```
mem_active_bank5 = '1' when mem_active_page = 0x0A or mem_active_page = 0x0B
mem_active_bank7 = '1' when mem_active_page = 0x0E
```

When these pages are active, `sram_active` is set to 0 (no external SRAM
access) and the CPU reads/writes through the BRAM interface instead.

Note: page 0x0F is NOT flagged as bank7 — only 0x0E.

### Memory Contention

Contention is enabled when ALL of:
- `nr_08_contention_disable = 0`
- NOT Pentagon timing
- CPU speed = 3.5 MHz (both speed bits = 0)

Memory contention applies based on timing mode and active page:
- Pages must be in the range 0x00-0x0F (16K banks 0-7) — `mem_active_page(7:4) = "0000"`
- **48K timing**: only bank 5 pages (0x0A, 0x0B) — `mem_active_page(3:1) = "101"`
- **128K timing**: odd banks (pages where bit 1 = 1) — `mem_active_page(1) = '1'`
- **+3 timing**: banks >= 4 (pages where bit 3 = 1) — `mem_active_page(3) = '1'`

Port contention: `(NOT cpu_a(0)) OR port_7ffd_active OR port_bf3b OR port_ff3b`

### Layer 2 Memory Mapping

When Layer 2 mapping is enabled via port 0x123B:
- Read enable: `port_123b_layer2_map_rd_en`
- Write enable: `port_123b_layer2_map_wr_en`
- Overrides MMU for the mapped address range

The L2 base bank is determined by NR 0x12 (active) or NR 0x13 (shadow),
with offset from port 0x123B segment selection. The mapping can be applied
to 0-16K, 16K-32K, 32K-48K, or "auto" (segment follows cpu_a(15:14)).

L2 mapping takes priority over MMU in the 0-16K region and optionally in
16K-48K. It does NOT apply to the 48K-64K region.

## Test Categories

### Category 1: MMU Slot Assignment (NR 0x50-0x57)

Direct NextREG writes to configure each MMU slot and verify the correct
physical page is mapped.

### Category 2: MMU Reset State

Verify all 8 MMU registers contain their documented reset values.

### Category 3: Legacy 128K Paging (Port 0x7FFD)

Standard 128K memory model — bank select for slot 6/7, ROM select, shadow
screen, and lock bit.

### Category 4: Extended Paging (Port 0xDFFD)

Extra bank bits extending the 128K model to 256K/512K/1024K.

### Category 5: +3 Paging (Port 0x1FFD)

+3 ROM selection and special all-RAM modes.

### Category 6: +3 Special Paging Modes

All four special paging configurations with full MMU state verification.

### Category 7: Paging Lock

Port 0x7FFD bit 5 lock, Pentagon-1024 lock override, and interaction with
NR 0x08 bit 7 unlock.

### Category 8: NR 0x8E Unified Paging

Single-register paging that updates 7FFD/1FFD/DFFD simultaneously.

### Category 9: Mapping Modes (NR 0x8F)

Pentagon 512K and Pentagon 1024K bank composition.

### Category 10: Port 0xEFF7

RAM-at-0x0000 mode and Pentagon-1024 disable.

### Category 11: ROM Selection

ROM page selection per machine type, with and without altrom lock.

### Category 12: Alternate ROM (NR 0x8C)

Enable/disable, read/write control, lock bits, and reset persistence.

### Category 13: Config Mode (NR 0x03/0x04)

ROMRAM bank mapping when config mode is active.

### Category 14: Address Translation

Verify the `mmu_A21_A13` formula produces correct physical addresses for
representative page values.

### Category 15: Bank 5/7 Special Pages

Dual-port BRAM routing for pages 0x0A, 0x0B, 0x0E.

### Category 16: Memory Contention

Contention rules per timing mode and speed, verifiable via T-state counts.

### Category 17: Layer 2 Memory Mapping

L2 overlay read/write enable, segment selection, bank offset.

### Category 18: Memory Decode Priority

Verify that DivMMC > Layer 2 > MMU > Config > ROM priority is respected
in the 0-16K region.

## Detailed Test Case Catalog

### Category 1: MMU Slot Assignment

| ID     | Test                           | Setup                    | Expected                                       |
|--------|--------------------------------|--------------------------|------------------------------------------------|
| MMU-01 | Write NR 0x50 = 0x00           | NR 0x50 ← 0x00          | Read at 0x0000 accesses page 0 (RAM bank 0 lo) |
| MMU-02 | Write NR 0x51 = 0x01           | NR 0x51 ← 0x01          | Read at 0x2000 accesses page 1 (RAM bank 0 hi) |
| MMU-03 | Write NR 0x52 = 0x04           | NR 0x52 ← 0x04          | Read at 0x4000 accesses page 4 (RAM bank 2 lo) |
| MMU-04 | Write NR 0x53 = 0x05           | NR 0x53 ← 0x05          | Read at 0x6000 accesses page 5 (RAM bank 2 hi) |
| MMU-05 | Write NR 0x54 = 0x0A           | NR 0x54 ← 0x0A          | Read at 0x8000 accesses page 10 (bank 5 lo)    |
| MMU-06 | Write NR 0x55 = 0x0B           | NR 0x55 ← 0x0B          | Read at 0xA000 accesses page 11 (bank 5 hi)    |
| MMU-07 | Write NR 0x56 = 0x0E           | NR 0x56 ← 0x0E          | Read at 0xC000 accesses page 14 (bank 7 lo)    |
| MMU-08 | Write NR 0x57 = 0x0F           | NR 0x57 ← 0x0F          | Read at 0xE000 accesses page 15 (bank 7 hi)    |
| MMU-09 | Write NR 0x50 = 0xFF           | NR 0x50 ← 0xFF          | Slot 0 maps to ROM (mmu_A21_A13(8) = 1)        |
| MMU-10 | High page (NR 0x54 = 0x40)     | NR 0x54 ← 0x40          | Page 64 maps to SRAM address 0x060000           |
| MMU-11 | Max page (NR 0x54 = 0xDF)      | NR 0x54 ← 0xDF          | Page 223, highest valid RAM page                |
| MMU-12 | Page 0xE0 overflows to ROM     | NR 0x54 ← 0xE0          | mmu_A21_A13(8)=1, treated as ROM                |
| MMU-13 | Read-back NR 0x50-0x57         | Write values, read back  | Each register returns the written value         |
| MMU-14 | Write/read pattern all slots   | Write 0x10-0x17 to slots | Each slot reads back correctly                  |
| MMU-15 | Slot boundary (0x1FFF/0x2000)  | MMU0=0x10, MMU1=0x20     | 0x1FFF in page 0x10, 0x2000 in page 0x20       |

### Category 2: MMU Reset State

| ID     | Test                   | Expected                                              |
|--------|------------------------|-------------------------------------------------------|
| RST-01 | MMU0 after reset       | 0xFF (ROM)                                            |
| RST-02 | MMU1 after reset       | 0xFF (ROM)                                            |
| RST-03 | MMU2 after reset       | 0x0A (bank 5 lo — screen RAM)                         |
| RST-04 | MMU3 after reset       | 0x0B (bank 5 hi)                                      |
| RST-05 | MMU4 after reset       | 0x04 (bank 2 lo)                                      |
| RST-06 | MMU5 after reset       | 0x05 (bank 2 hi)                                      |
| RST-07 | MMU6 after reset       | 0x00 (bank 0 lo)                                      |
| RST-08 | MMU7 after reset       | 0x01 (bank 0 hi)                                      |

### Category 3: Legacy 128K Paging (Port 0x7FFD)

| ID      | Test                          | Setup                        | Expected                              |
|---------|-------------------------------|------------------------------|---------------------------------------|
| P7F-01  | Bank 0 select                 | 0x7FFD ← 0x00               | MMU6=0x00, MMU7=0x01                  |
| P7F-02  | Bank 1 select                 | 0x7FFD ← 0x01               | MMU6=0x02, MMU7=0x03                  |
| P7F-03  | Bank 2 select                 | 0x7FFD ← 0x02               | MMU6=0x04, MMU7=0x05                  |
| P7F-04  | Bank 3 select                 | 0x7FFD ← 0x03               | MMU6=0x06, MMU7=0x07                  |
| P7F-05  | Bank 4 select                 | 0x7FFD ← 0x04               | MMU6=0x08, MMU7=0x09                  |
| P7F-06  | Bank 5 select                 | 0x7FFD ← 0x05               | MMU6=0x0A, MMU7=0x0B                  |
| P7F-07  | Bank 6 select                 | 0x7FFD ← 0x06               | MMU6=0x0C, MMU7=0x0D                  |
| P7F-08  | Bank 7 select                 | 0x7FFD ← 0x07               | MMU6=0x0E, MMU7=0x0F                  |
| P7F-09  | ROM 0 select                  | 0x7FFD ← 0x00               | MMU0=0xFF, MMU1=0xFF, ROM 0 active    |
| P7F-10  | ROM 1 select (bit 4)          | 0x7FFD ← 0x10               | MMU0=0xFF, MMU1=0xFF, ROM 1 active    |
| P7F-11  | Shadow screen (bit 3)         | 0x7FFD ← 0x08               | port_7ffd_shadow = 1                  |
| P7F-12  | Lock bit (bit 5)              | 0x7FFD ← 0x20               | port_7ffd_locked = 1                  |
| P7F-13  | Locked write rejected         | Lock, then write 0x7FFD ← 1 | MMU6/7 unchanged                      |
| P7F-14  | NR 0x08 bit 7 unlocks         | Lock, NR 0x08 ← 0x80        | port_7ffd_reg(5) cleared → unlocked   |
| P7F-15  | Full register preserved       | 0x7FFD ← 0xC7               | All bits stored correctly             |
| P7F-16  | Shadow disables Timex `screen_mode` | 0x7FFD ← 0x08 (bit 3 = 1), verify `screen_mode` | `screen_mode` forced to `"000"` when shadow asserted (VHDL `zxula.vhd:191`) — re-home from ULA S15.03 |
| P7F-17  | Bit 3 → `Ula::set_shadow_screen_en` routing | 0x7FFD ← 0x08, then 0x7FFD ← 0x00 | `Ula::shadow_screen_en()` flips 0→1→0 per write; `i_ula_shadow_en` wiring at `zxnext.vhd:4453` — re-home from ULA S15.04 |

P7F-16 and P7F-17 are re-home rows added 2026-04-24 per
`doc/design/TASK-MMU-SHADOW-SCREEN-PLAN.md`. They reopen `mmu_test`
(148/0/0 → 150/0/2) with reason `F-SHADOW-WIRING` until the
`Mmu::write_port_7ffd` handler forwards bit 3 to `Ula::set_shadow_screen_en`
(same one-line fix pattern as the NR 0x68 bit 3 → `set_ulap_en` fix landed
as `a1495ba`).

### Category 4: Extended Paging (Port 0xDFFD)

| ID      | Test                          | Setup                        | Expected                              |
|---------|-------------------------------|------------------------------|---------------------------------------|
| DFF-01  | Extra bit 0                   | DFFD ← 0x01, 7FFD ← 0x00   | port_7ffd_bank(3) = 1 → bank 8       |
| DFF-02  | Extra bit 1                   | DFFD ← 0x02, 7FFD ← 0x00   | port_7ffd_bank(4) = 1 → bank 16      |
| DFF-03  | Extra bit 2                   | DFFD ← 0x04, 7FFD ← 0x00   | port_7ffd_bank(5) = 1 → bank 32      |
| DFF-04  | Extra bit 3                   | DFFD ← 0x08, 7FFD ← 0x00   | port_7ffd_bank(6) = 1 → bank 64      |
| DFF-05  | Max bank (DFFD=0x0F,7FFD=7)   | DFFD ← 0x0F, 7FFD ← 0x07   | port_7ffd_bank = 127                  |
| DFF-06  | Locked by 7FFD bit 5          | Lock, DFFD ← 0x01           | DFFD register unchanged               |
| DFF-07  | Bit 4 (Profi DFFD override)   | DFFD ← 0x10                 | No effect (Profi disabled in VHDL)    |
| DFF-08  | Soft reset preserves DFFD     | DFFD ← 0x0F, reset(false)   | port_dffd_reg preserved, MMU6/7 reflect preserved bank (VHDL:3687) |
| DFF-09  | DFFD bit 6 round-trip via Mmu accessor | OUT 0xDFFD ← 0x40 / 0x4A / 0x0A / 0xCA; observe `Mmu::port_dffd_reg_6()` and `port_dffd_reg()` | Bit 6 latched into separate `port_dffd_reg_6_` flip-flop; vector storage independent; hard reset clears bit 6. VHDL `zxnext.vhd:877,3686-3689,3693-3694,4314`. pass (G148 closed on `task8-t1w2-mmu-v2`) |

### Category 5: +3 Paging (Port 0x1FFD)

| ID      | Test                          | Setup                        | Expected                              |
|---------|-------------------------------|------------------------------|---------------------------------------|
| P1F-01  | ROM bank 0 (+3 mode)          | 1FFD ← 0x00, 7FFD bit4=0    | port_1ffd_rom = "00", ROM 0           |
| P1F-02  | ROM bank 1 (+3 mode)          | 7FFD ← 0x10                  | port_1ffd_rom = "01", ROM 1           |
| P1F-03  | ROM bank 2 (+3 mode)          | 1FFD ← 0x04                  | port_1ffd_rom = "10", ROM 2           |
| P1F-04  | ROM bank 3 (+3 mode)          | 1FFD ← 0x04, 7FFD ← 0x10   | port_1ffd_rom = "11", ROM 3           |
| P1F-05  | Special mode enable            | 1FFD ← 0x01                  | port_1ffd_special = 1, all-RAM        |
| P1F-06  | Locked by 7FFD bit 5          | Lock, 1FFD ← 0x01           | 1FFD register unchanged               |
| P1F-07  | Motor bit independent          | 1FFD ← 0x08                  | Disk motor on, paging unaffected      |

### Category 6: +3 Special Paging Modes

| ID      | Config bits (2:1) | MMU0 | MMU1 | MMU2 | MMU3 | MMU4 | MMU5 | MMU6 | MMU7 |
|---------|-------------------|------|------|------|------|------|------|------|------|
| SPE-01  | 00 (1FFD=0x01)    | 0x00 | 0x01 | 0x02 | 0x03 | 0x04 | 0x05 | 0x06 | 0x07 |
| SPE-02  | 01 (1FFD=0x03)    | 0x08 | 0x09 | 0x0A | 0x0B | 0x0C | 0x0D | 0x0E | 0x0F |
| SPE-03  | 10 (1FFD=0x05)    | 0x08 | 0x09 | 0x0A | 0x0B | 0x0C | 0x0D | 0x06 | 0x07 |
| SPE-04  | 11 (1FFD=0x07)    | 0x08 | 0x09 | 0x0E | 0x0F | 0x0C | 0x0D | 0x06 | 0x07 |
| SPE-05  | Exit special mode  | 1FFD ← 0x00 after SPE-01     | MMU2-5 restored to defaults, MMU6/7 from 7FFD |

### Category 7: Paging Lock

| ID      | Test                              | Expected                                        |
|---------|-----------------------------------|-------------------------------------------------|
| LCK-01  | 7FFD bit 5 locks 7FFD writes      | Subsequent 7FFD writes ignored                  |
| LCK-02  | 7FFD bit 5 locks 1FFD writes      | Subsequent 1FFD writes ignored                  |
| LCK-03  | 7FFD bit 5 locks DFFD writes      | Subsequent DFFD writes ignored                  |
| LCK-04  | NR 0x08 bit 7 clears lock         | 7FFD(5) ← 0, writes accepted again             |
| LCK-05  | Pentagon-1024 overrides lock      | NR 0x8F ← 0x03, EFF7(2)=0 → locked=0          |
| LCK-06  | MMU writes bypass lock            | NR 0x50 write succeeds even when locked          |
| LCK-07  | NR 0x8E bypasses lock             | NR 0x8E write always takes effect                |

### Category 8: NR 0x8E Unified Paging

| ID      | Test                              | Write        | Expected                                      |
|---------|-----------------------------------|--------------|-----------------------------------------------|
| N8E-01  | Bank select (bit 3=1)             | 0x8E ← 0xB8 | 7FFD(2:0)=0x03(bits6:4), dffd(0)=1(bit7)     |
| N8E-02  | ROM select (bit 3=0, bit 2=0)    | 0x8E ← 0x01 | 7FFD(4)=1 (bit 0), ROM 1 selected             |
| N8E-03  | Special mode via 8E               | 0x8E ← 0x04 | port_1ffd_reg(0)=1, special mode enabled      |
| N8E-04  | Special + config bits             | 0x8E ← 0x07 | 1ffd_reg = (2:0) = special=1, bits=11         |
| N8E-05  | Read-back format                  | Various      | Matches documented read-back formula           |
| N8E-06  | Bank select clears DFFD(3)        | 0x8E ← 0x88 | dffd_reg(3)=0 when not Profi                  |

### Category 9: Mapping Modes (NR 0x8F)

| ID      | Test                              | Setup                      | Expected                                      |
|---------|-----------------------------------|----------------------------|-----------------------------------------------|
| N8F-01  | Standard mode (default)           | NR 0x8F ← 0x00            | Normal bank composition                        |
| N8F-02  | Pentagon 512K                     | NR 0x8F ← 0x02            | 7FFD bits 7:6 used for bank(4:3)              |
| N8F-03  | Pentagon 1024K                    | NR 0x8F ← 0x03, EFF7(2)=0 | Lock override active, bank(5) from 7FFD(5)    |
| N8F-04  | Pentagon 1024K disabled by EFF7   | NR 0x8F ← 0x03, EFF7(2)=1 | Lock override NOT active                       |
| N8F-05  | Pentagon bank(6) always 0         | NR 0x8F ← 0x02, DFFD=0x08 | port_7ffd_bank(6) = 0                          |

### Category 10: Port 0xEFF7

| ID      | Test                              | Setup                      | Expected                                      |
|---------|-----------------------------------|----------------------------|-----------------------------------------------|
| EF7-01  | Bit 3 = RAM at 0x0000             | EFF7 ← 0x08               | MMU0=0x00, MMU1=0x01 (on next paging change)  |
| EF7-02  | Bit 3 = 0 → ROM at 0x0000        | EFF7 ← 0x00               | MMU0=0xFF, MMU1=0xFF (on next paging change)  |
| EF7-03  | Bit 2 = 1 disables Pent-1024     | NR 0x8F=0x03, EFF7 ← 0x04 | pentagon_1024_en = 0, lock is NOT overridden   |
| EF7-04  | Reset state                       | After reset                | port_eff7_reg_2 = 0, port_eff7_reg_3 = 0     |
| EF7-05  | Soft reset preserves EFF7 + RAM-at-0 | EFF7 ← 0x0C, reset(false) | port_eff7_reg_{2,3} preserved, slots 0/1 stay RAM (VHDL:3777) |
| EF7-06  | NR 0x85 b2 (`port_eff7_io_en`) gates EFF7 writes | NR 0x85 b2 ← 0; OUT 0xEFF7 ← 0x0C (Pent-1024 disable + RAM-at-0); follow with the usual paging-change trigger | `port_eff7_reg_{2,3}` stays 0; MMU0 stays at ROM. VHDL `zxnext.vhd:2604, 2441, 2392` ANDs port-decode with `internal_port_enable(26)` which sits in the nr_85 byte (= NR 0x85 bit 2). **G143 fix landed** in `emulator.cpp` 2026-04-28; **NR mapping corrected to NR 0x85 b2 on 2026-05-04** during Tier A SKIP-reduction. RE-HOMED to `test/mmu/mmu_integration_test.cpp` (MMU-EF7-IO-EN-00..02). |

### Category 11: ROM Selection

| ID      | Test                              | Machine     | Setup                  | Expected ROM         |
|---------|-----------------------------------|-------------|------------------------|----------------------|
| ROM-01  | 48K always ROM 0                  | 48K         | Default                | sram_rom = "00"      |
| ROM-02  | 128K ROM 0                        | 128K        | 7FFD bit 4 = 0        | sram_rom = "00"      |
| ROM-03  | 128K ROM 1                        | 128K        | 7FFD bit 4 = 1        | sram_rom = "01"      |
| ROM-04  | +3 ROM 0                          | +3          | 1FFD=0, 7FFD bit4=0   | sram_rom = "00"      |
| ROM-05  | +3 ROM 1                          | +3          | 7FFD bit 4 = 1        | sram_rom = "01"      |
| ROM-06  | +3 ROM 2                          | +3          | 1FFD bit 2 = 1        | sram_rom = "10"      |
| ROM-07  | +3 ROM 3                          | +3          | 1FFD=4, 7FFD=0x10     | sram_rom = "11"      |
| ROM-08  | ROM is read-only                  | Any         | Write to ROM space     | Write has no effect   |
| ROM-09  | ROM with altrom_rw = 1            | NR 8C=0xC0  | Write to ROM space     | Write succeeds        |
| ROM-10  | 48K hardwires `sram_rom3=1`       | 48K         | Cycle 7FFD/1FFD/lock states | `Mmu::sram_rom3()` always true regardless of port / altrom state per VHDL `zxnext.vhd:2985`. **Implemented** — accessor returns `true` for `MachineType::ZX48K` (G57 closed) |
| ROM-11  | NR 0x8C altrom-lock factor        | ZXN         | Lock_rom1 only vs lock_rom0 only | `sram_rom3()` follows `nr_8c_altrom_lock_rom1` per VHDL `:3000`; lock_rom0 alone returns false. **Implemented** — accessor branches on locks (G57 closed) |
| ROM-12  | port_1ffd(2) discrimination       | +3 vs ZXN   | (1FFD=4, 7FFD=0/0x10) on +3; (1FFD=0/4, 7FFD=0/0x10) on ZXN | +3: `sram_rom3 = 1FFD(2) AND 7FFD(4)` per VHDL `:2994`. ZXN: `sram_rom3 = 7FFD(4)` alone per VHDL `:3004` (1FFD bit 2 has no effect on the non-+3 branch). **Implemented** — accessor switches on `machine_type_` (G57 closed) |

### Category 11.x: Boot ROM Overlay (`bootrom_en`)

| ID         | Test                                          | Setup                                                                  | Expected                                                                                                |
|------------|-----------------------------------------------|------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------|
| BOOT-OVL-01 | 8 KB boot ROM overlays full 16 KB at 0x0000-0x3FFF | Load 8 KB blob, `bootrom_en=1`, read 0x0000..0x1FFF and 0x2000..0x3FFF | All four 8 KB quarters return blob bytes; upper 8 KB mirrors lower 8 KB per VHDL `bootrom_mod = cpu_a(12:0)` (`zxnext.vhd:3199-3204`); decode gate is `cpu_a(15:14)="00"` (`zxnext.vhd:1856`). **Implemented** — `Mmu::read` gates `addr < 0x4000` and indexes `addr & 0x1FFF` (G140 closed) |
| BOOT-OVL-02 | Boot ROM does not leak past 0x3FFF | Same fixture, read 0x4000 | Read falls through to MMU slot 2, NOT to boot ROM (gate is `cpu_a(15:14)="00"`, `zxnext.vhd:1856`). **Implemented** — gate now uses `addr < 0x4000` (G140 closed) |
| BOOT-OVL-03 | Wrong-sized boot ROM blob raises a diagnostic | Load a 4 KB (or 12 KB) blob via `set_boot_rom`; expected behaviour = warn + clamp to 8 KB | `Mmu::set_boot_rom` materialises an 8 KB internal buffer (zero-pad / truncate) and emits a warn-level diagnostic. VHDL hardwires `cpu_a(12:0)` (8 KB span) at `zxnext.vhd:3199-3204`. **Implemented** — `boot_rom_buf_` 8 KB always (G157 closed) |

### Category 12: Alternate ROM (NR 0x8C)

| ID      | Test                              | Setup             | Expected                                      |
|---------|-----------------------------------|--------------------|-----------------------------------------------|
| ALT-01  | Enable altrom                     | NR 0x8C ← 0x80   | Reads from alt ROM area in SRAM               |
| ALT-02  | Disable altrom                    | NR 0x8C ← 0x00   | Reads from standard ROM area                   |
| ALT-03  | Altrom read/write enable          | NR 0x8C ← 0xC0   | ROM space is writable                          |
| ALT-04  | Altrom read-only                  | NR 0x8C ← 0x80   | ROM space is read-only                         |
| ALT-05  | Lock ROM1                         | NR 0x8C ← 0xA0   | ROM selection forced to ROM1 regardless of 7FFD|
| ALT-06  | Lock ROM0                         | NR 0x8C ← 0x90   | ROM selection forced to ROM0                   |
| ALT-07  | Reset preserves bits 3:0          | Set 0x8C=0x85     | After reset, bits 7:4 = old bits 3:0 = 0x05   |
| ALT-08  | Altrom address 128K               | NR 0x8C ← 0x80   | sram_A21_A13 = "0000011" & alt_128_n & a(13)  |
| ALT-09  | Read-back                         | NR 0x8C ← 0xA5   | Reading NR 0x8C returns 0xA5                   |

**Reset-domain rows for the two lock bits (GH #191).** ALT-07 asserts the
nibble copy as a whole *byte* at the bare-`Mmu` tier. It does not assert the
decoded `nr_8c_altrom_lock_rom1` / `nr_8c_altrom_lock_rom0` flags, and GH #190
showed both of those were claimed only by traceability rows grafted onto other
tests. The two rows below are the real coverage. They are **hosted in
`test/nextreg/nextreg_integration_test.cpp` (group `Reset-Domain`)** because
they drive the reset through the real NR 0x02 path at the Emulator tier, which
is the only tier where hard and soft reset are distinguishable at all.

The VHDL says the lock bits are **reloaded, not cleared**: `zxnext.vhd:2254-2255`
copies `nr_8c_altrom(3 downto 0)` into `(7 downto 4)` on `reset='1'`, and
`:2264`/`:2265` decode bits 5/4 as `lock_rom1`/`lock_rom0`. `nextreg.txt:861-865`
documents the same ("AFTER SOFT RESET (copied into bits 7-4)"). The pair is
deliberately mirror-imaged so it discriminates bit 5 from bit 4.

| ID         | Test                                | Setup                         | Expected                                            |
|------------|-------------------------------------|-------------------------------|-----------------------------------------------------|
| RSTD-8C-01 | `lock_rom1` reloaded from bit 1      | NR 0x8C ← 0x02, then RESET_SOFT | lock_rom1=1, lock_rom0=0, NR 0x8C reads 0x22       |
| RSTD-8C-02 | `lock_rom0` reloaded from bit 0      | NR 0x8C ← 0x01, then RESET_SOFT | lock_rom0=1, lock_rom1=0, NR 0x8C reads 0x11       |

### Category 13: Config Mode (NR 0x03/0x04)

The VHDL branch chain at `zxnext.vhd:3030-3057` decides what a CPU access to a
ROM-mapped slot (0x0000-0x3FFF) does, in this priority order:

1. `mf_mem_en='1'` (Multiface) — :3030
2. `mmu_A21_A13(8)='0'` (an MMU-RAM page is mapped there) — :3037
3. `nr_03_config_mode='1'` → SRAM at `nr_04_romram_bank & cpu_a(13)`, `sram_pre_rdonly<='0'` (writeable) — :3044-3050
4. otherwise → `"000000" & sram_rom & cpu_a(13)`, `sram_pre_rdonly <= not (altrom_en and altrom_rw)` (normally read-only) — :3051-3057

| ID      | Test                              | Setup                       | Expected                                      |
|---------|-----------------------------------|-----------------------------|-----------------------------------------------|
| CFG-01  | Config mode maps ROMRAM, writeably | config_mode=1, NR 0x04=n    | Writes to 0x0000-0x3FFF land in SRAM at `(n<<1) \| slot` and are NOT dropped — branch 3, `zxnext.vhd:3044-3045` (address) + `:3049` (`sram_pre_rdonly<='0'`) |
| CFG-02  | Config mode read path             | config_mode=1, NR 0x04=n    | Reads from 0x0000-0x3FFF return the SRAM bank contents, not the ROM image — same branch, `zxnext.vhd:3044-3045` |
| CFG-03  | MMU-RAM mapping wins over config mode | config_mode=1 **and** an MMU-RAM page mapped on slot 0 | Access lands in the mapped RAM page, not the NR 0x04 bank — branch 2 is tested first (`zxnext.vhd:3037`) |
| CFG-04  | Config mode off → normal ROM      | config_mode=0               | 0x0000-0x3FFF follows normal ROM selection and writes drop (`sram_pre_rdonly` set) — branch 4, `zxnext.vhd:3051-3057` |

> **GH #193 — this table was corrected against the VHDL, not against the tests.**
> Three of the four rows previously named behaviour the suite asserts under a
> different ID (old CFG-02 ↔ test CFG-04, old CFG-03 ↔ test CFG-01), and old
> CFG-03's "MMU-RAM wins" case was absent from the plan entirely. Each row above
> was re-derived from the branch chain and matches what `mmu_test.cpp` asserts.
>
> **The old CFG-04 ("after reset, `nr_03_config_mode = 1`") was WRONG and is
> deleted, not renumbered.** `nr_03_config_mode` is declared `:= '1'` at
> `zxnext.vhd:1102` — an FPGA **power-on** signal initialiser — and is assigned
> nowhere else except the NR 0x03 write handler at `:5148` / `:5150`. It does
> **not** appear in the `if reset = '1'` block of the NR state process
> (`zxnext.vhd:4930-5108`), which in fact *reads* it at `:5109`
> (`if nr_03_config_mode = '1' then bootrom_en <= '1'`). The latch therefore
> **survives reset**. That is a NextREG-tier behaviour, not an MMU one: it is
> owned by `NextReg` (see G62) and pinned by `nextreg_test.cpp` **CFG-07**
> ("reset() preserves config_mode"), plus **CFG-08** for the soft-reset case.
> `mmu_test` CFG-06 documents the mirror side of the same fact.

Rows CFG-05..CFG-12 in `mmu_test.cpp` extend this category (bit-13 half-bank
select, reset behaviour, out-of-range banks, setter round-trip, and the
`rom_in_sram` branch-4 variants) and are recorded in the traceability matrix's
"Extra coverage (not in plan)" table for this suite.

#### NR 0x04 `nr_04_romram_bank` reset domain (GH #194)

**The VHDL PRESERVES `nr_04_romram_bank` across reset.**
`grep -n nr_04_romram_bank zxnext.vhd` returns exactly four sites — `:1104`
(signal declaration with its power-on initialiser `(others => '0')`), `:3045`
(the use in the branch chain above), and `:5717` / `:5732` (the NR 0x04 write
handlers of `gen_romram_234` / `gen_romram_5`, each in a generate process whose
only clause is `if nr_04_we = '1'`). The signal is **absent from the NR state
process's `if reset = '1'` block at `:4930-5111`**, and `reset` there is
`reset_hard or reset_soft` (`zxnext_top_issue2.vhd:840`, `zxnext.vhd:1730`), so
neither reset arm touches it. A declaration initialiser applies at FPGA
configuration, not at every reset.

jnext previously cleared it in **both** `Mmu::reset()` and `NextReg::reset()`,
citing `:1104` — the same declaration-default-mistaken-for-a-reset-clause error
already fixed for `nr_03_config_mode` (G62) and `nr_03_machine_type` (G63).
`mmu_test` **CFG-06** asserted the clearing as correct and was corrected to
assert preservation.

A hardware HARD reset *does* reconfigure the FPGA
(`zxnext_top_issue2.vhd:1195` starts flashboot on `zxn_reset_hard`), so the
`:1104` default genuinely applies there. jnext reproduces that by
**reconstructing** the emulator in `emulator_cold_boot()`. The two paths must
therefore differ, and are proven apart:

| ID         | Test                                          | Setup                                             | Expected                                          |
|------------|-----------------------------------------------|---------------------------------------------------|---------------------------------------------------|
| CFG-06     | `Mmu::reset()` (hard arm) preserves the bank  | config_mode=1, NR 0x04=0x30, `reset()`            | slot-0 write still routes to SRAM page 96, not 0  |
| CFG-12     | `Mmu::reset(hard=false)` preserves it too     | config_mode=1, NR 0x04=0x30, `reset(false)`       | same — VHDL `reset` covers both arms              |
| RSTD-04-01 | RESET_SOFT preserves `nr_04_romram_bank`      | NR 0x04 ← 0x30, then NR 0x02 ← 0x01               | `nextreg().nr_04_romram_bank()` still 0x30        |
| RSTD-04-02 | RESET_HARD clears it via the host cold boot   | NR 0x04 ← 0x30, NR 0x02 ← 0x02, `emulator_cold_boot()` | request raised, bank still 0x30 mid-way, 0x00 after |
| RSTD-04-03 | NR 0x04 write reaches the **Mmu mirror**, bit 7 masked | NR 0x04 ← 0x30, then ← 0xB7            | `mmu().nr_04_romram_bank()` 0x30 then 0x37; latch agrees |
| RSTD-04-04 | RESET_SOFT preserves the **Mmu mirror** too   | NR 0x04 ← 0x30, then NR 0x02 ← 0x01               | `mmu().nr_04_romram_bank()` still 0x30, equal to the latch |
| RSTD-04-05 | the **boot-ROM-gated resync block** in `Emulator::init()` | boot ROM loaded, NR 0x04 ← 0x30, NR 0x03 ← 0x03 (clears the latch's config_mode), `Mmu` config_mode forced true, then NR 0x02 ← 0x01 | gate entered; `mmu().config_mode()` followed the latch to false; `mmu().nr_04_romram_bank()` undisturbed at 0x30 |

CFG-06 / CFG-12 are `mmu_test.cpp`; RSTD-04-01..04 are hosted in
`test/nextreg/nextreg_integration_test.cpp` (group `Reset-Domain`), the only
tier where hard and soft reset are distinguishable. CFG-12 exists because the
`hard` flag makes a one-armed model possible: the mutation
`if (!hard) nr_04_romram_bank_ = 0;` passed CFG-06 and RSTD-04-01/02, and
CFG-12 is what catches it.

**GH #195 — why -03/-04 exist.** The four rows above split by *which mirror*
they observe, because jnext holds the one VHDL signal twice: the `NextReg`
latch and an `Mmu` mirror (the SRAM address compose at `zxnext.vhd:3045` is on
the `Mmu` hot path). -01/-02 read the latch; CFG-05..12 drive a **bare `Mmu`
with no `Emulator`**. So no row observed the mirror *through* the emulator, and
a reviewer mutation of the resync in `Emulator::init()` escaped all 6560 unit
rows and both NextZXOS boot/reset functional rows. -03/-04 close that by
asserting `Emulator::mmu().nr_04_romram_bank()` — an accessor added for exactly
this (the earlier absence of which is what forced CFG-12 down to the bare tier).

**RSTD-04-05 exists because -03/-04 were not enough**, and the #195 review is
what proved it. Neither of those rows leaves `boot_rom_enabled()` true, so
`init()`'s `if (cfg.type == ZXN_ISSUE2 && mmu_.boot_rom_enabled())` block is
never entered by any unit fixture — the reviewer planted a *fresh*
`set_nr_04_romram_bank(0)` inside it and it escaped all 6562 unit rows and both
NextZXOS reset functional rows, the identical signature that got #195 filed.
-05 enters the block (a loaded boot ROM plus config_mode re-arms the overlay in
`Mmu::reset()`, `mmu.cpp:179`) and asserts `gate_taken` so it cannot go vacuous.

It also pins the resync that **survived**. `mmu_.set_config_mode(nextreg_...)`
is load-bearing precisely where its nr_04 sibling was not: those two mirrors
have DIFFERENT power-on defaults (`NextReg` true per `zxnext.vhd:1102`, `Mmu`
false per `mmu.h`), so they really can disagree. -05 drives them apart on
purpose before the reset — a row that left them already equal would pass
against a deleted resync. Deleting the line fails -05 (`mmu_cfg=1`); so does
hardcoding it. That mirror also had no getter until #195 added one, which is
why nothing had ever asserted it.

That investigation also found the mutated line was **dead**: the two mirrors
share a power-on default, both preserve across reset, and their only mutators
are paired, so the `init()` resync was an identity assignment. Measured, not
argued — a temporary divergence probe at the site reported zero divergences
across the whole unit suite and a full firmware boot + F4 soft reset. The line
is deleted; -03/-04 pin the invariant it pretended to enforce.

### Category 14: Address Translation

| ID      | Page  | Expected mmu_A21_A13    | Physical base address |
|---------|-------|-------------------------|-----------------------|
| ADR-01  | 0x00  | 0_00100000 (0x020)      | 0x040000              |
| ADR-02  | 0x01  | 0_00100001 (0x021)      | 0x042000              |
| ADR-03  | 0x0A  | 0_00101010 (0x02A)      | 0x054000              |
| ADR-04  | 0x0B  | 0_00101011 (0x02B)      | 0x056000              |
| ADR-05  | 0x0E  | 0_00101110 (0x02E)      | 0x05C000              |
| ADR-06  | 0x10  | 0_00110000 (0x030)      | 0x060000              |
| ADR-07  | 0x20  | 0_01000000 (0x040)      | 0x080000              |
| ADR-08  | 0xDF  | 0_11111111 (0x0FF)      | 0x1FE000              |
| ADR-09  | 0xE0  | 1_00000000 (0x100)      | Overflow → ROM path   |
| ADR-10  | 0xFF  | 1_00011111 (0x11F)      | Overflow → ROM path   |

Formula verification: for page P, `mmu_A21_A13 = ((1 + P[7:5]) << 5) | P[4:0]`.
Physical address = `mmu_A21_A13 << 13`.

### Category 15: Bank 5/7 Special Pages

| ID      | Test                              | Setup             | Expected                                      |
|---------|-----------------------------------|--------------------|-----------------------------------------------|
| BNK-01  | Page 0x0A → bank5 path           | MMU4 = 0x0A       | sram_bank5=1, sram_active=0 — write lands in the dedicated bank-5 VRAM buffer, no SRAM page touched (Task 25 rewrite; previously asserted the aliased SRAM-page-0x0A model) |
| BNK-02  | Page 0x0B → bank5 path           | MMU4 = 0x0B       | sram_bank5=1, sram_active=0 — write lands at VRAM offset 0x2000, no SRAM page touched (Task 25 rewrite) |
| BNK-03  | Page 0x0E → bank7 path           | MMU6 = 0x0E       | sram_bank7=1, sram_active=0                   |
| BNK-04  | Page 0x0F → normal SRAM          | MMU6 = 0x0F       | sram_bank7=0, sram_active=1                   |
| BNK-05  | Bank5 read/write functional       | Write to 0x0A page| Data readable back through bank5 BRAM         |
| BNK-06  | Bank7 read/write functional       | Write to 0x0E page| Data readable back through bank7 BRAM         |

### Category 16: Memory Contention

| ID      | Test                              | Timing | Speed   | Page  | Expected         |
|---------|-----------------------------------|--------|---------|-------|------------------|
| CON-01  | 48K: bank 5 contended             | 48K    | 3.5 MHz | 0x0A  | Contended        |
| CON-02  | 48K: bank 5 hi contended          | 48K    | 3.5 MHz | 0x0B  | Contended        |
| CON-03  | 48K: bank 0 not contended         | 48K    | 3.5 MHz | 0x00  | Not contended    |
| CON-04  | 48K: bank 7 not contended         | 48K    | 3.5 MHz | 0x0E  | Not contended    |
| CON-05  | 128K: odd banks contended         | 128K   | 3.5 MHz | 0x03  | Contended        |
| CON-06  | 128K: even banks not contended    | 128K   | 3.5 MHz | 0x04  | Not contended    |
| CON-07  | +3: banks >= 4 contended          | +3     | 3.5 MHz | 0x08  | Contended        |
| CON-08  | +3: banks < 4 not contended       | +3     | 3.5 MHz | 0x06  | Not contended    |
| CON-09  | High page never contended         | 48K    | 3.5 MHz | 0x10  | Not contended    |
| CON-10  | NR 0x08 bit 6 disables contention | 48K    | 3.5 MHz | 0x0A  | Not contended    |
| CON-11  | Speed > 3.5 MHz no contention     | 48K    | 7 MHz   | 0x0A  | Not contended    |
| CON-12a | Pentagon timing: machine type falls through switch | Pent | 3.5 MHz | 0x0A | Not contended    |
| CON-12b | Pentagon timing: gate zeros 48K bank 5 contention  | 48K  | 3.5 MHz | 0x0A + set_pentagon_timing(true) | Not contended |

### Category 17: Layer 2 Memory Mapping

| ID      | Test                              | Setup                           | Expected                              |
|---------|-----------------------------------|---------------------------------|---------------------------------------|
| L2M-01  | L2 write-over routes writes to L2 bank, not to unrelated MMU page | MMU0→page 0x20, L2 bank 8, write 0x0000=0xAB, read via MMU | MMU page 0x20 unchanged (L2 write landed in physical page 0x10) |
| L2M-01b | L2 bank 8 physically aliases MMU page 0x10 (hw collision) | MMU0→page 0x10, L2 bank 8, write 0x0000=0xAB, read via MMU | MMU page 0x10 reads 0xAB — same SRAM per VHDL zxnext.vhd:2964,2969,2971 |
| L2M-02a | L2 read-enable redirects 0x0000-0x3FFF reads to L2 bank | MMU0→page 0x20 (0x55), L2 bank 8 page 0x10=0xA7, set_l2_port(0x04,8), read(0x0000) | 0xA7 — L2 bank wins per VHDL zxnext.vhd:2969,3077,3100 |
| L2M-02b | L2 read-enable OFF → MMU slot wins (discriminative) | Same setup as 02a but set_l2_port(0x00,8) | 0x55 — MMU slot per VHDL zxnext.vhd:3077 (sram_pre_layer2_rd_en required) |
| L2M-03  | L2 auto segment follows A(15:14) | port_123b seg=11                | Segment = cpu_a(15:14)                |
| L2M-04  | L2 does NOT map 48K-64K          | port_123b seg=11, access 0xC000 | MMU used, not L2                       |
| L2M-05  | L2 bank from NR 0x12             | NR 0x12 = bank, shadow=0       | L2 base bank matches NR 0x12          |
| L2M-06  | L2 shadow bank from NR 0x13      | NR 0x13 = bank, shadow=1       | L2 base bank matches NR 0x13          |

### Category 18: Memory Decode Priority

| ID      | Test                              | Setup                           | Expected                              |
|---------|-----------------------------------|---------------------------------|---------------------------------------|
| PRI-01  | DivMMC ROM overrides MMU          | DivMMC active + MMU configured  | DivMMC ROM at sram 0x010000           |
| PRI-02  | DivMMC RAM overrides MMU          | DivMMC RAM active               | DivMMC RAM bank selected              |
| PRI-03  | L2 overrides MMU in 0-16K        | L2 + MMU both configured        | L2 address used                        |
| PRI-04  | L2 does not override DivMMC      | DivMMC + L2 both active         | DivMMC wins (higher priority)          |
| PRI-05  | MMU page in upper 48K            | Only MMU configured             | MMU address used (no overrides)        |
| PRI-06  | Altrom overrides normal ROM       | altrom_en=1, ROM space          | Alt ROM address used                   |
| PRI-07  | Config mode overrides ROM         | config_mode=1, ROM space        | ROMRAM bank address used               |

### Category 19: Soundrive Mode 2 vs Paging-Port Write Conflict (G146)

VHDL `zxnext.vhd:2708` raises `port_fd_conflict_wr` when the OUT's **low
byte** is 0xF1 or 0xF9 (`port_f1_lsb`/`port_f9_lsb` decode the full 8-bit
`cpu_a(7:0)`, `zxnext.vhd:2508-2576`; high byte don't-care) AND the
Soundrive-SD2 decode is enabled (`port_dac_sd2_ABCD_f1f3f9fb_io_en` =
`internal_port_enable(18)` = **NR 0x84 bit 2**, `zxnext.vhd:2429-2430`).
`zxnext.vhd:2718-2720, 2725` then suppress `port_7ffd_wr` / `port_dffd_wr`
/ `port_1ffd_wr` / `port_3ffd_wr` entirely; the byte still reaches the
Soundrive DAC channel (`zxnext.vhd:2775-2778`). 0xF3/0xFB (channels B/D)
have A1:A0="11" and can never alias the FD family — the VHDL omits them.

> **Correction (Task 57, 2026-07-14):** the original wording of these rows
> ("0xF1FD/0xF3FD/0xF9FD/0xFBFD writes", "NR 0x84 b1") was wrong on both
> counts — those addresses have low byte 0xFD and never conflict, and the
> gate is bit 2, not bit 1. Colliding addresses are e.g. 0x7FF1 / 0xDFF9 /
> 0x1FF1 (low byte F1/F9 + paging high bits).

**Re-homed (Task 57, 2026-07-14):** the conflict lives in the
port-dispatch layer (Emulator handler wiring); the bare-Mmu fixture of
`mmu_test` cannot reach it. Both rows are implemented in
`test/audio/audio_port_dispatch_test.cpp` (group SD2); `mmu_test` Cat 19
carries COVERED-AT comments.

| ID     | Test                                  | Setup                                                                  | Expected                                                                                          |
|--------|---------------------------------------|------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------|
| SD2-01 | SD2-on suppresses colliding paging writes | NR 0x84 b2=1; baseline 7FFD=0x02 / DFFD=0x03 / 1FFD=0x04 via low-byte-0xFD OUTs; OUT 0x7FF1←0x05, 0xDFF9←0x06, 0x1FF1←0x07 | 7FFD/DFFD/1FFD retain 0x02/0x03/0x04 (`zxnext.vhd:2708, 2718-2720`); bytes land on Soundrive ch A/C (`zxnext.vhd:2775-2778`). PASS — audio_port_dispatch_test SD2-01 |
| SD2-02 | SD2-off lets the same writes through  | NR 0x84 b2=0 (0xFB); same three OUTs                                    | 7FFD=0x05, DFFD=0x06, 1FFD=0x04 (conflict term 0); DAC untouched. PASS — audio_port_dispatch_test SD2-02 |

### Category 20: NEX Loader (parked here as `BOOT-NEX-*`)

> Note: NEX-loader rows are parked in this plan as `BOOT-NEX-*`. A
> future `BOOT-NEX-TEST-PLAN-DESIGN.md` may split them out into a
> dedicated `nex_loader_test.cpp`. Until then they live here because
> the loader writes through `Mmu`.

| ID         | Test                                                          | Setup                                                                                                  | Expected                                                                                              |
|------------|---------------------------------------------------------------|--------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------|
| BOOT-NEX-01 | Loader rejects NEX whose `ram_required` exceeds installed RAM | Load a synthetic NEX V1.1 with `ram_required=2` (2 MB) on a 1 MB-installed `Ram`                       | Loader returns an error/warning; bank loads do NOT proceed. **Implemented** — `NexLoader::apply` aborts via `ram_required_fits` predicate (G155 closed) |
| BOOT-NEX-02 | Loader accepts NEX when `ram_required` ≤ installed RAM        | Same NEX with `ram_required=0` (768 KB) on default 2048 KB Ram                                         | Loader proceeds, banks land at expected offsets. **Implemented** — discriminative test on `ram_required_fits`/`ram_required_kb` (G155 closed) |
| BOOT-NEX-03 | Per-bank loading bar rendered    | `NexLoader::render_progress_mark(mmu, d, colour)` called at 3 distinct bank-slot indices (d=0,1,5), colour=0x07 (white) | Each call writes a real 4-byte mark into physical bank 11 (MMU page 23) at a `d`-derived, strictly-increasing byte offset — "advances along VRAM" per nexload.asm:616-621 `progress`. **Implemented** — `NexLoader::render_progress_mark` (G156 closed 2026-07-13; residual limitation noted there: not visible on a bare `--load`, only proven via a Layer-2-forced scratch harness) |
| BOOT-NEX-04 | Inter-bank `loading_delay` honoured | `NexLoader::inter_bank_delay_frames(screen_present, loading_delay=10)` | `screen_present=true` -> 109 x 10 = 1090 frames (nexload.asm:541,612-614 gate: the 109-iteration post-early loop, per-slot regardless of bank presence); `screen_present=false` -> 0. **Implemented** — pure function, unit-tested + mutation-tested (G156 closed 2026-07-13) |
| BOOT-NEX-05 | `start_delay` before code-entry  | `NexLoader::boot_hold_frames(start_delay=50, screen_present, loading_delay=10)` | Adds the unconditional `start_delay` (nexload.asm:575-577) on top of any inter-bank total, even with no screen data: `boot_hold_frames(50,true,10)==1140`, `boot_hold_frames(50,false,10)==50`. Actual CPU-hold enforcement (no instruction fetched while `Emulator::boot_hold_frames_remaining_ > 0`) is exercised separately by `mmu_integration_test.cpp` G156-HOLD-01..09 (PC/R frozen across held frames, positive-control resume, save/load round-trip mid-hold). **Implemented** (G156 closed 2026-07-13) |
| BOOT-NEX-06 | Loading-bar colour honoured      | Two `render_progress_mark` calls at distinct bank-slot indices with colour=0x02 (red) and colour=0x07 (white) | Each mark's 4 bytes match its own requested colour, not a hardcoded default. **Implemented** (G156 closed 2026-07-13) |
| BOOT-NEX-07 | NEX loader writes to physical bank 5 do NOT leak ULA attributes | Load synthetic NEX whose bank-5 payload contains non-zero bytes spanning the 0x1800-0x1AFF attribute range; on default-RAM Next, take a screenshot 100 ms post-entry | ULA attribute area in screenshot reflects only the post-entry-point ULA writes — NOT the loader's transient bank-5 fill. skip — NEX loader writes raw bytes via `Mmu::write_byte` without a loader-bank-5 audit gate; cosmetic (see G16) — `nex_loader.cpp:177-223` (bank-5 ingest paths); `BEAST-NEX-INVESTIGATION.md` §Verdict |

### Category 21: SD Card Hot-Plug / Unmount (parked here as `BOOT-SD-*`)

> Note: G158 is a runtime-UX gap (real Next has CD/CS detect; jnext's
> `SdCardDevice::mount`/`unmount` exist but are invoked only once at
> startup from `src/core/emulator.cpp:2197-2200`). Rows live in
> `test/sdcard/sdcard_test.cpp` because the mount/unmount API is
> reachable there. Future GUI menu work should rebind once a visible
> affordance lands.

| ID        | Test                                  | Setup                                                                | Expected                                                                                    |
|-----------|---------------------------------------|----------------------------------------------------------------------|---------------------------------------------------------------------------------------------|
| BOOT-SD-01 | mount → unmount → re-mount round-trip | `sd.mount(img)`; issue CMD17 read; `sd.unmount()`; `sd.mount(img)`; CMD17 read | First read returns image bytes; post-unmount no spurious data leak; second mount reads same bytes. skip — runtime API not exposed in GUI/CLI (see G158) |
| BOOT-SD-02 | unmount mid-transfer is safe          | `sd.mount(img)`; begin CMD17; call `sd.unmount()` mid-block          | No data race; subsequent reads return safe-default (0xFF). VHDL: card-detect/CS controls. skip — mid-transfer safety untested (see G158) |

### Category 22: Tape SAVE Pipeline (parked here as `BOOT-TAPESAVE-*`)

> Note (updated Task 57 — G33 **Phase 1 CLOSED**): `TapSaver`
> (`src/core/tap_saver.{h,cpp}`) now exists — trap-based SAVE→TAP at the
> 48K ROM SA-BYTES routine (`0x04C2`, entry bytes verified against the
> extracted 48.rom; register convention A=flag, IX=start, DE=length per
> "The Complete Spectrum ROM Disassembly"), armed via `--tape-save FILE`.
> The rows below exercise the TAP block builder / file-append / loader
> round-trip directly (byte-array fixtures, same technique as `BOOT-Z80-*`;
> mmu_test does not link jnext_core — `TapSaver` block/file APIs and
> `TapLoader::parse_blocks` are header-inline for exactly this reason,
> z80_loader.h precedent). Expected bytes are hand-computed from the TAP
> spec, never from TapSaver's own output. The full-Emulator tier — the
> trap handler and the run_frame() arming gate, including the ROM-identity
> signature check added after the review found the plain PC gate corrupting
> a NextZXOS boot — is covered by `mmu_integration_test`
> MMU-G33-TRAP-01..03 plus the `tape-save-boot-func` regression row.
> The original Phase 2/3 row ideas
> (analogue MIC→TZX capture, WAV writer) remain open under G33 and get
> their own rows when those savers exist.

| ID            | Test                                                       | Setup                                                                                                         | Expected                                                                                                          |
|---------------|------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------|
| BOOT-TAPESAVE-01 | TAP header block byte layout                            | `TapSaver::build_block(0x00, 17-byte Program header payload)`                                                  | Exact 21-byte image: `13 00` LE length (17+2), flag `00`, payload verbatim, XOR checksum `0x72` (hand-computed) |
| BOOT-TAPESAVE-02 | TAP data block + multi-block file append ordering       | `build_block(0xFF, 5-byte payload)`; then `set_output(tmp)` + `append_block` header then data                  | Exact 9-byte data image (checksum `0xDC` hand-computed); file bytes == header-block ‖ data-block, in append order |
| BOOT-TAPESAVE-03 | Saver→loader round-trip                                 | Concatenated `build_block` outputs parsed by `TapLoader::parse_blocks` (the exact loop `TapLoader::load` runs) | 2 blocks; flags 0x00/0xFF; payload identity with inputs; `verify_checksum()` true; zero parse warnings |

### Category 23: `.z80` Snapshot Loader (parked here as `BOOT-Z80-*`)

> Note (updated Task 13b — CLOSED): `.z80` (v1/v2/v3) is the canonical
> FUSE/SPIN snapshot format alongside `.sna`/`.szx`. `Z80Loader`
> (`src/core/z80_loader.{h,cpp}`) now exists and is wired into CLI, Qt
> file dialogs, and `Emulator::load_z80()`. Rows below run against
> `Z80Loader::load_from_buffer()` / `apply_ram_to_mmu()` directly (not
> `--load`, since `mmu_test` does not link `Emulator`/`jnext_core` —
> same technique `NexLoader`'s inline static helpers use). Parked here
> because the loader writes through `Mmu`.

| ID         | Test                                                  | Setup                                                                                                         | Expected                                                                                                                              |
|------------|-------------------------------------------------------|---------------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------|
| BOOT-Z80-01 | v1 (uncompressed) .z80 round-trip                     | in-test byte-array v1 header (48K, PC!=0 sentinel) + 49152-byte raw RAM body                                  | `Mmu` reads at 0x4000-0xFFFF match the raw-RAM image; PC/AF/BC etc. match header bytes.        |
| BOOT-Z80-02 | v2 (RLE-compressed) .z80 round-trip                   | in-test byte-array v2 header; 3 pages RLE-encoded with `ED ED nn xx` runs + literal tail                      | Decompressor reproduces matching RAM image across all three 48K page->bank mappings.           |
| BOOT-Z80-03 | v3 (extended-header, 128K) .z80                       | in-test byte-array v3 header, 54-byte extension, 8 × 16 KB uncompressed pages (3..10)                         | `Ram` banks 0-7 populated; `port_7ffd_` from header byte 0x23.                                  |
| BOOT-Z80-04 | Unsupported / corrupt .z80 file rejected              | in-test byte-array truncated v1-compressed body (no end marker)                                               | Loader returns error; `Mmu` stays untouched (canary byte survives).                             |
| BOOT-Z80-05 | Structurally-valid .z80 whose pages are all foreign page numbers is rejected | in-test byte-array v3/128K header, single page with page_num=255 (outside 3..10) | `load_from_buffer()` succeeds (parsing is structurally fine) but `apply_ram_to_mmu()` returns false — zero pages applied is a load failure, not a silent no-op success. Independent-review finding (post-Task-13b): without this guard the loader reported success with zero RAM written. Canary byte in `Mmu` survives untouched. |

### Category 24: Snapshot Save Pipeline (parked here as `BOOT-SNAPSAVE-*`)

> Note (Task 13b): `sna_saver.*`, `szx_saver.*`, and `nex_saver.*` all
> exist and are wired to File > Save Snapshot... (GUI) and
> `--delayed-snapshot` (headless CLI). Parked here because the save
> path must read `Mmu`/`Cpu`/peripheral state and serialise — the same
> bus the loaders use.

| ID            | Test                                                | Setup                                                                                                | Expected                                                                                                                           |
|---------------|-----------------------------------------------------|------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------|
| BOOT-SNAPSAVE-01 | `.sna` save round-trip via GUI/CLI               | Run program; trigger a save action; reload the produced `.sna`                                       | Reloaded state byte-equal on RAM + visible registers. skip — `sna_saver` exists but no UI/CLI consumer wires it (see G35) |
| BOOT-SNAPSAVE-02 | `.szx` save round-trip via GUI/CLI               | Same; save as `.szx`                                                                                 | pass (Task 13b, REDESIGNED 2026-07-13) — `.szx` is scoped to a classic Spectrum's 8-bank memory model: 48K (`chMachineId=1`, banks {5,2,0} only — `chPageNo` per libspectrum `szx.c:3330-3334`, NOT jnext's own bank numbering "first N"), 128K/+2A/+3 (`chMachineId=2/4/5`, all 8 banks). Full CPU register set, classic paging (7FFD/1FFD), and border round-trip byte-for-byte for every supported machine (`mmu_test.cpp` BOOT-SNAPSAVE-02/02B/02C/02D structural + `mmu_integration_test.cpp` SNAPSAVE-SZX-RT (+3)/SNAPSAVE-SZX-RT-48K (48K)/SNAPSAVE-SZX-RT-REFUSED full pipeline). Next (and any other unsupported machine) is **refused outright** — `SzxSaver::save()` returns no data and a clear error, never a truncated or misrepresenting file — since jnext's default `--machine` is Next, this is the common path, not an edge case. **Independently verified against real `fuse`/`libspectrum` 1.5.0** (not just jnext's own `SzxLoader`): saved 48K/128K/+3 snapshots all load via `libspectrum_snap_read()` with correct `chMachineId`→`libspectrum_machine` mapping and full RAM content; 128K/+3 additionally verified pixel-identical through a save→reload→screenshot round trip via jnext's own renderer. First cut of this redesign (2026-07-13 overnight) instead raised the RAM ceiling to 64 banks/1024 KB for ALL machines — rejected by the user as the wrong shape: `.szx` is a classic-interchange format, not a place to widen for Next RAM. NextREG/video-layer/peripheral state does NOT round-trip — no standard zx-state block exists for it (see `SzxSaver` class doc-comment). |
| BOOT-SNAPSAVE-03 | `.nex` save round-trip via GUI/CLI               | Same; save as `.nex` v1.2                                                                            | pass (Task 13b) — PC/SP/border/entry_bank/RAM banks (up to the 112-bank ceiling) round-trip (`mmu_test.cpp` BOOT-SNAPSAVE-03/03B/03C structural + `mmu_integration_test.cpp` SNAPSAVE-NEX-RT full pipeline). Does NOT "re-run from the same state" for an arbitrary mid-execution snapshot: the NEX header has no field for AF/BC/DE/HL/IX/IY/alt-set/I/R/IFF/IM, and `Emulator::load_nex()` resets before applying, so only PC/SP survive (see `NexSaver` class doc-comment; demonstrated live via a headless save→reload screenshot: a mid-BASIC-execution `.nex` snapshot reloads to a black screen because the resumed registers don't match the saved RAM/PC context). |
| BOOT-SNAPSAVE-04 | Save-As dialog exposes all three formats         | Open File→Save-As                                                                                    | Filter shows `.sna`, `.szx`, `.nex` entries. skip — no Save-As dialog wired (see G35)                                                  |

### Category 25: Tape DeciLoad / Real-time Loading (parked here as `BOOT-DECI-*`)

> Note: G36 covered TZX block 0x15 (Direct-Recording, used by DeciLoad
> and similar custom-loader schemes); G37 covered WAV-as-tape
> real-time loading. **Both CLOSED by Task 57 (2026-07-14)** — see
> `doc/issues/deciload-tzx/DECILOAD-TZX-LOADING.md` §RESOLVED for the
> three root causes (frame-relative tape clock; ZOT pause swallowing
> the block-terminating edge; WAV edge quantisation to the sample
> grid). Rows REDESIGNED at un-skip time to pin the decoder-level
> contracts the fixes established (the original 01/03 end-to-end
> "loader completes" shape lives in the `xevious-deciload` screenshot
> regression row, which loads the full game through 0x11+0x15 blocks
> in `--tape-realtime` mode; original 02 "malformed block tolerated"
> was superseded — ZOT's parser already stops cleanly on a truncated
> body, and the discriminative surface of the ACTUAL bugs is the pause
> edge semantics now pinned by the new 02).

| ID         | Test                                                            | Setup                                                                                          | Expected                                                                                                              |
|------------|-----------------------------------------------------------------|------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------|
| BOOT-DECI-01 | TZX 0x15 Direct-Recording level-vs-time contract              | Synthetic in-memory TZX: one 0x15 block, 77 T/sample, 13 samples, used_bits=5                  | pass (Task 57) — EAR(t) = sample[(t-t0)/77] MSB-first for every 7 T probe across the block; honours used_bits; level 0 + stopped after the final sample (TZX spec v1.20 block 0x15) |
| BOOT-DECI-02 | TZX pause preserves the block-terminating edge                | Synthetic 0x14 block ending low + 100 ms pause; 0x15 blocks ending high/low + 20 ms pause      | pass (Task 57) — pause holds the final level 3500 T (1 ms) then drops low (empirically-derived heuristic — the measured 48K-ROM LD-BYTES terminating-edge requirement; NOT libspectrum behaviour, which treats the pause start as an ordinary toggle edge); 0x15 final sample level survives un-inverted; no phantom high after a low final sample |
| BOOT-DECI-03 | WAV real-time EAR threshold playback                          | Synthetic 8-bit mono 44.1 kHz WAV (high/low/high runs), `WavLoader::get_ear_bit()`             | pass (Task 57) — T-states map to sample frames against the 3.5 MHz clock; level = amplitude vs 128 centre; 0 before playback start and past end of data |
| BOOT-DECI-04 | WAV sub-sample interpolation of edge timing                   | Synthetic WAV with a 96→160 crossing (threshold crossed halfway between frames)                | pass (Task 57) — EAR transitions at the linearly-interpolated crossing, not the 79.4 T grid: probe at frame 10.75 reads 1 where stepwise thresholding reads 0 (the DeciLoad short/long margin discriminator) |

### Category 26: `.dsk` / +3 FDC Loading (parked here as `BOOT-FDC-*`)

> Note: jnext does not model the uPD765 FDC. P1F-07 in this plan is
> already a WONT decision (commit `3dd892a`, 2026-04-21,
> *"+3 disk motor — explicit decision NOT to implement; NextZXOS /
> SD / NEX loaders cover relevant software"*). Tracked for
> completeness so any future re-evaluation has pre-allocated rows.
> Future implementer must lift P1F-07 first.

| ID         | Test                                            | Setup                                                                  | Expected                                                                                                |
|------------|-------------------------------------------------|------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------|
| BOOT-FDC-01 | `.dsk` (CPCEMU/EDSK) image mounted on +3 drive | `--machine plus3 --load game.dsk`                                       | FDC enumerates tracks/sectors; +3DOS recognises disk and shows catalog. skip — no uPD765 / no `.dsk` loader; P1F-07 = WONT (see G38) |
| BOOT-FDC-02 | uPD765 motor-on / read-id behaviour            | Issue Read-ID command; check status registers                          | ST0/ST1/ST2 and CHRN bytes per uPD765 datasheet. skip — uPD765 unmodelled; P1F-07 = WONT (see G38)        |
| BOOT-FDC-03 | NR 0x81 b3 (`fdc` clken) gates motor-on        | NR 0x81 ← bit3=1; observe drive-motor LED state via NR introspection   | Motor-on visible; NR 0x81 b3=0 ⇒ motor off. skip — FDC unmodelled; P1F-07 = WONT (see G38)              |

### Category 28: Dedicated bank-5 / bank-7 BRAMs

VHDL gives banks 5 and 7-lower dedicated dual-port BRAMs, physically
separate from external SRAM: `bank5_ram` (16K dpram2,
`zxnext.vhd:6558-6578`) and `bank7_ram` (8K dpram2, `zxnext.vhd:6670`).
`mem_active_bank5/bank7` (`:2961-2962`) suppress the external-SRAM cycle
(`sram_pre_active`, `:3037-3041/:3059-3063`); the config-mode NR $04
window (`:3044-3050`), the CPU Layer 2 window (`:3100-3107`) and the L2
pixel fetch (`layer2.vhd:170-190`) always address external SRAM, even
when their computed physical page is 0x0A/0x0B/0x0E. The BANK7 rows
landed with the 2026-07-09 NextZXOS boot fix; the BANK5 rows are Task 25
(2026-07-10), root cause of the Task 23 mid-boot screen garbage.

| ID       | Test                                                    | Setup                                                          | Expected                                                                              |
|----------|---------------------------------------------------------|----------------------------------------------------------------|----------------------------------------------------------------------------------------|
| BANK7-01 | MMU page 0x0E served from dedicated BRAM                | Next mode; MMU slot → 0x0E; write                              | Byte lands in `bank7_bram_`, not SRAM page 0x0E or 0x2E                                 |
| BANK7-02 | Phys-page-0x0E write invisible through MMU page 0x0E    | Write via MMU page 0x0E; poke `ram_` page 0x0E                 | MMU read returns MMU-written bytes (the $DA35 NextZXOS boot killer)                     |
| BANK7-03 | MMU page 0x0A served from dedicated bank-5 VRAM         | Next mode; MMU slot → 0x0A; write                              | Byte lands in `bank5_vram_`, not SRAM page 0x0A or 0x2A (rewritten by Task 25 — the row previously asserted the aliased pre-fix model) |
| BANK7-04 | Config-mode NR $04=$17 window vs bank-7 BRAM isolation  | Seed BRAM via MMU page 0x0E; config window write               | SRAM page 0x2E written; BRAM byte intact                                                |
| BANK7-05 | Standalone machines keep bank 7 flat                    | rom_in_sram=false; 128K bank-7 paging; write both 8K halves    | Bytes land in `ram_` pages 0x0E/0x0F, not the BRAM                                      |
| BANK5-01 | Pages 0x0A/0x0B = lower/upper halves of one 16K VRAM    | Next mode; slots → 0x0A + 0x0B; write both                     | Offsets 0x0000/0x2000 of `bank5_vram_`; read-back through MMU intact                    |
| BANK5-02 | Config-mode NR $04=$05 window vs bank-5 VRAM isolation  | Seed VRAM via MMU page 0x0A; config window write (enNextMf.rom path) | SRAM page 0x0A written; VRAM byte intact (the Task 23 mid-boot-garbage regression) |
| BANK5-03 | Standalone machines keep bank 5 flat                    | rom_in_sram=false; write 0x4000/0x6000                         | Bytes land in `ram_` pages 0x0A/0x0B, not the VRAM                                      |
| BANK5-04 | CPU L2 window bank 5 targets SRAM 0x2A (no bypass)      | Next mode; port $123B write-over, bank 5; write 0x0000         | Byte lands in `ram_` page 0x2A (unconditional `layer2_A21_A13`), not 0x0A, not the VRAM |

**Total: ~168 test cases across 28 categories.**

## Test Approach

### Unit Test Runner (Primary)

A dedicated C++ test runner (`mmu_test.cpp`) following the Z80N test pattern:

1. **Direct MMU/memory subsystem testing** — instantiate the memory subsystem in
   isolation (or with minimal CPU/IO wiring) and exercise NextREG writes, port
   writes, and memory access through the public API.

2. **Test data format** — each test case specifies:
   - Test name
   - Machine type (48K, 128K, +3, Pentagon, Next)
   - Sequence of NextREG/port writes (register, value)
   - Memory access to perform (address, read/write)
   - Expected MMU register state (all 8 slots)
   - Expected physical address or data result
   - Expected flags (contended, read-only, bank5/bank7 path)

3. **Test data files** — `test/mmu/tests.in` and `test/mmu/tests.expected`,
   hand-computed from VHDL analysis.

4. **Minimal test IO** — a `MMUTestIO` class that captures NextREG writes and
   port writes, configurable machine type, and allows direct MMU register
   inspection.

### Demo Programs (Secondary, for Integration)

For tests that are difficult to validate in a unit runner (e.g., contention
timing, Layer 2 overlay visual correctness), Z80 demo programs that:
- Map specific pages and write known patterns
- Read back and verify via checksum
- Display pass/fail on screen
- Run in headless mode with screenshot comparison

### Contention Tests

Contention is best tested by measuring T-state counts for memory accesses to
known-contended vs. non-contended pages. The unit runner can compare actual
T-states consumed vs. expected.

## Integration Notes

### Minimal C++ Changes Required

1. **Test runner executable** — new `test/mmu_test.cpp` with its own `main()`,
   linked against `jnext_core`.

2. **Memory subsystem access** — the test runner needs to:
   - Create a `Memory` (or equivalent) object
   - Write NextREGs via the existing nextreg handler interface
   - Write legacy ports (0x7FFD, 0x1FFD, etc.) via the IO interface
   - Read MMU register values (NR 0x50-0x57 read-back)
   - Perform memory reads/writes and observe the physical address resolved
   - Query contention state for a given address

3. **CMake additions** — in `test/CMakeLists.txt`:
   ```cmake
   add_executable(mmu_test mmu_test.cpp)
   target_link_libraries(mmu_test PRIVATE jnext_core)
   file(COPY mmu/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/mmu)
   add_test(NAME mmu_test COMMAND mmu_test ${CMAKE_CURRENT_BINARY_DIR}/mmu)
   ```

4. **Regression script** — add MMU test phase to `test/regression.sh`:
   ```
   [mmu]     Running MMU/memory compliance tests...
     PASS: 120/120 tests passed
   ```

### Dependencies

- No new external libraries required
- Test runner reuses existing `jnext_core` library
- Test data files are self-contained (no ROM files needed for most tests)
- Contention tests may need a minimal ULA timing stub

## Open Questions

1. **Should contention tests use real ULA timing or a stub?** Real timing gives
   higher fidelity but couples the test to ULA correctness. A stub isolates the
   memory subsystem.

2. **Layer 2 mapping tests** require the L2 bank configuration (NR 0x12/0x13)
   and port 0x123B state. The test runner needs access to these registers.

3. **DivMMC priority tests** require the DivMMC automap state machine. These may
   be better suited for a separate DivMMC test suite with only the priority
   interaction tested here.

4. **Pentagon/Profi modes** — Profi is disabled in VHDL. Should we test the
   disabled state (verify it has no effect) or skip entirely?

## References

- VHDL source: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`
  - MMU registers: lines 1018-1025
  - Reset state: lines 4610-4618
  - Address translation: line 2964
  - Memory decode: lines 2933-3133
  - 128K paging: lines 3640-3814
  - Contention: lines 4481-4496
  - NR 0x8E: lines 3662-3734
  - Altrom: lines 2247-2265
  - Config mode: lines 3044-3050
  - Layer 2 mapping: lines 2966-2971, 3077, 3100-3107
