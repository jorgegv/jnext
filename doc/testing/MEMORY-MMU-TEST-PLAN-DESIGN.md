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

Measured on main 2026-04-20 post-Phase-2-A merge:

- **145 plan rows total** (DFF-08 + EF7-05 added by Phase 2 A), mapped 1:1 to test IDs (152 check()+skip() calls in test).
- **113 pass, 0 fail, 39 skip.**
- Phase 2 C0 (commit `354fa14`) landed NR 0x08 bit 7 paging unlock — un-skipped P7F-14 and LCK-04.
- Phase 1a re-triage: un-skipped BNK-01..04 (dual-port bypass outcome tests). MMU-12, ADR-09, ADR-10 were initially un-skipped but REVERTED to skip() after independent critic review flagged SX-02 anti-pattern (tests encoded JNEXT's `to_sram_page` truncation as the oracle instead of VHDL's `sram_pre_active=0` floating-bus semantics per zxnext.vhd:3060-3061).
- Phase 2 C (`fix/mmu-branch-c`) un-skipped 16 rows: ROM-01..07 (machine-type / sram_rom accessor per zxnext.vhd:2981-3008), ALT-01..07 + ALT-09 (NR 0x8C altrom register storage + decoded accessors), plus a bonus un-skip for RW-02 in the integration tier via the NR 0x08 read handler (bit 7 = NOT paging-lock, bit 6 = contention-disable).
- Phase 2 C also added `Mmu::reset(bool hard)` overload: VHDL-faithful soft reset now preserves `paging_locked_`, `contention_disabled_`, and NR 0x8C bits 3:0 across RESET_SOFT (all three previously cleared unconditionally — a pre-existing divergence from C0 that Branch C took care of while adding the NR 0x08 bit 6 + NR 0x8C state). VHDL citations: zxnext.vhd:1730 (hard-reset signal), 2253-2256 (NR 0x8C nibble copy), 3646-3648 (port_7ffd_reg clear), 4930-4935 (contention_disable clear).
- Phase 2 A (`fix/mmu-branch-a`) un-skipped 12 rows (DFF-01..07, LCK-03, EF7-01..04) and added 2 new rows (DFF-08, EF7-05) covering soft-reset preservation — all 14 passing. Implemented `Mmu::write_port_dffd` (lock-gated per VHDL:3691) + `Mmu::write_port_eff7` (ungated per VHDL:3781); EFF7 bit 3 re-maps slots 0/1 to RAM pages 0x00/0x01 per VHDL:4636-4644; DFFD bank composition `port_7ffd(2:0) | (port_dffd(4:0)<<3)` per VHDL:3763-3766. Soft reset preserves both registers (VHDL:3687, :3777) AND their downstream page-map effects (DFFD→MMU6/7, EFF7→MMU0/1) via a post-seed `apply_legacy_paging_()` call in `Mmu::reset(false)` — emulator must re-assert because our MMU state is imperative where VHDL is combinational.
- **Previously-listed RST-01/RST-02 failures**: already fixed by earlier reset-seed work — all eight RST rows pass (MMU0/MMU1 seed to the 0xFF ROM sentinel per VHDL zxnext.vhd:4611-4618).
- **Remaining 39 skips blocked by** Phase 2 branches B (NR 0x8E + NR 0x8F unified-paging; LCK-05 and LCK-07 shared with A/B), D1 (ContentionModel inputs: mem_active_page, CPU speed, Pentagon timing), D2 (Layer 2 read-port), plus 3 DivMmc-overlay rows (PRI-01/02/04) destined for integration tier, 2 altrom SRAM-arbiter overrides (ALT-08, ROM-09 — need full sram_pre_rdonly wiring), and NR 0x12/0x13 shadow (integration tier).
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
| N8E-01  | Bank select (bit 3=1)             | 0x8E ← 0xB8 | 7FFD(2:0)=0x07(bits6:4), dffd(0)=1(bit7)     |
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

### Category 13: Config Mode (NR 0x03/0x04)

| ID      | Test                              | Setup                       | Expected                                      |
|---------|-----------------------------------|-----------------------------|-----------------------------------------------|
| CFG-01  | Config mode maps ROMRAM           | config_mode=1, NR 0x04=0x10 | 0x0000-0x3FFF mapped to ROMRAM bank 0x10      |
| CFG-02  | Config mode off → normal ROM      | config_mode=0               | 0x0000-0x3FFF follows normal ROM selection     |
| CFG-03  | ROMRAM bank writeable             | config_mode=1               | Writes to 0x0000-0x3FFF succeed               |
| CFG-04  | Config mode at reset              | After reset                 | nr_03_config_mode = 1                          |

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
| BNK-01  | Page 0x0A → bank5 path           | MMU4 = 0x0A       | sram_bank5=1, sram_active=0                   |
| BNK-02  | Page 0x0B → bank5 path           | MMU4 = 0x0B       | sram_bank5=1, sram_active=0                   |
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
| CON-06  | 128K: even banks not contended    | 128K   | 3.5 MHz | 0x02  | Not contended    |
| CON-07  | +3: banks >= 4 contended          | +3     | 3.5 MHz | 0x08  | Contended        |
| CON-08  | +3: banks < 4 not contended       | +3     | 3.5 MHz | 0x06  | Not contended    |
| CON-09  | High page never contended         | 48K    | 3.5 MHz | 0x10  | Not contended    |
| CON-10  | NR 0x08 bit 6 disables contention | 48K    | 3.5 MHz | 0x0A  | Not contended    |
| CON-11  | Speed > 3.5 MHz no contention     | 48K    | 7 MHz   | 0x0A  | Not contended    |
| CON-12  | Pentagon timing no contention      | Pent   | 3.5 MHz | 0x0A  | Not contended    |

### Category 17: Layer 2 Memory Mapping

| ID      | Test                              | Setup                           | Expected                              |
|---------|-----------------------------------|---------------------------------|---------------------------------------|
| L2M-01  | L2 write-over routes writes to L2 bank, not to unrelated MMU page | MMU0→page 0x20, L2 bank 8, write 0x0000=0xAB, read via MMU | MMU page 0x20 unchanged (L2 write landed in physical page 0x10) |
| L2M-01b | L2 bank 8 physically aliases MMU page 0x10 (hw collision) | MMU0→page 0x10, L2 bank 8, write 0x0000=0xAB, read via MMU | MMU page 0x10 reads 0xAB — same SRAM per VHDL zxnext.vhd:2964,2969,2971 |
| L2M-02  | L2 read-enable maps 0-16K        | port_123b read_en=1, seg=00     | Reads from L2 bank, not ROM/MMU       |
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

**Total: ~120 test cases across 18 categories.**

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
