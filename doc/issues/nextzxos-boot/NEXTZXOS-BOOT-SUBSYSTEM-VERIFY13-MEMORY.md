# Pass-13 Blind Audit — Memory subsystem

**Branch:** `task2/verify13-memory`
**Worktree:** `.claude/worktrees/task2-verify13-memory`
**Integration HEAD at start:** `adcc752`
**Date:** 2026-05-10

## Methodology

This pass focused on Pass-13 angles missed by the prior twelve audits:

- Save/load state coverage of every persisted memory-subsystem field.
- Cross-cutting NR write fan-out into shared latches (mirroring the
  Pass-12 `V12-NMP-01` "single FF, multiple writers" pattern but for
  memory NRs).
- Per-machine VHDL behavior differences (48K / 128K / +3 / Next /
  Pentagon).
- Interaction matrices between altrom × sram_rom × port_7ffd ×
  port_1ffd × DivMMC × Multiface × Layer 2 × config_mode.
- Hard-vs-soft-vs-NMI reset domains for every memory-subsystem latch.
- VHDL `*_q` / registered vs combinational value choices in C++.
- NR write-mask boundaries (reserved / sticky bits).
- Cold-boot defaults (Layer 2 base bank, sprite base, NR 0x50-0x57).

## Findings

### V13-MEM-01 — class-(a) — NR 0x69 bit 7 must fan out into Mmu's `l2_enable_` mirror

**VHDL line range:** zxnext.vhd:3914-3925, 3933 (`port_123b_layer2_en` FF
process and port 0x123B read-back composition).

**Root cause:** VHDL `port_123b_layer2_en` is a SINGLE flip-flop (declared
implicitly via :3908 reset assignment) that has TWO writers per
:3914-3925:

1. Port 0x123B write with bit 4 = 0 — `cpu_do(1)` latched (line 3916).
2. NR 0x69 write — `nr_wr_dat(7)` latched (line 3925).

The port 0x123B read-back at :3933 surfaces this FF as bit 1 of
`port_123b_dat`.

jnext mirrors the FF in TWO places (intentional split for cross-subsystem
ergonomics):

- `Layer2::enabled_` — used by the NR 0x69 read handler at
  `emulator.cpp:2179-2185` (composes NR 0x69 bit 7 from
  `layer2_.enabled()`).
- `Mmu::l2_enable_` — used by `Mmu::l2_port_readback()` at
  `mmu.h:1046-1054`, in turn used by the port 0x123B read handler at
  `emulator.cpp:2634-2638`.

The port 0x123B WRITE handler at `emulator.cpp:2639-2653` updates BOTH
mirrors — `Mmu` via `set_l2_port()` (which sets `l2_enable_`) and
`Layer2` via `layer2_.set_enabled()` (line 2651-2653). So a port 0x123B
write keeps both surfaces in sync.

The NR 0x69 write handler at `emulator.cpp:2148-2172` (pre-fix) updated
ONLY `Layer2::enabled_`. `Mmu::l2_enable_` was left stale until the next
port 0x123B write — diverging from VHDL where both writers feed the
same FF.

**Concrete bug surface:** firmware sequence

```
NEXTREG $69, $80    ; bit 7 = 1 — enable Layer 2 display
IN A, ($123B)        ; read port 0x123B
```

VHDL: bit 1 of returned byte = `port_123b_layer2_en` = 1.
jnext (pre-fix): bit 1 = `Mmu::l2_enable_` = 0 (stale from before the
NR 0x69 write).

Symmetric NR 0x69 read after a port 0x123B write was unaffected —
`layer2_.set_enabled` is called by both writers, so the NR 0x69 read
path stayed correct. The bug is one-directional: NR 0x69 → port 0x123B
read.

**Fix:** add `Mmu::set_l2_enable(bool)` setter (mmu.h, after
`set_l2_active_bank`) and call it from the NR 0x69 write handler so both
mirrors track the same FF.

```c++
// mmu.h
void set_l2_enable(bool en) { l2_enable_ = en; }
bool l2_enable() const { return l2_enable_; }

// emulator.cpp NR 0x69 write handler
mmu_.set_l2_enable((v & 0x80) != 0);
```

Mirrors the existing cross-subsystem pattern used by `set_l2_active_bank`
(NR 0x12 → Mmu) and `set_l2_shadow_bank` (NR 0x13 → Mmu).

**Discriminative regression test:** added 5 rows in
`test/mmu/mmu_integration_test.cpp` under group `V13-MEM-01-L2EN`:

- `V13-MEM-01-A` — baseline: clearing both NR 0x69 and port 0x123B
  yields port 0x123B bit 1 = 0.
- `V13-MEM-01-B` — **the bug surface**: write `NEXTREG $69, $80`, then
  read port 0x123B; bit 1 must = 1. *FAILS without the fix* (verified
  by temporarily reverting `mmu_.set_l2_enable` and running the test).
- `V13-MEM-01-C` — parallel guard: NR 0x69 read still returns bit 7 = 1
  via the Layer2 mirror (regression guard for the pre-fix path).
- `V13-MEM-01-D` — sweep: write `NEXTREG $69, $00`; port 0x123B bit 1
  must clear (proves the fix is not a one-shot raise).
- `V13-MEM-01-E` — independence guard: NR 0x69 fan-out must NOT
  perturb other port 0x123B bits (segment, shadow, rd_en, wr_en —
  VHDL :3924-3925 touches only `port_123b_layer2_en`).

**Discriminative verification:**

```
With fix:    16 passed, 0 failed (all five V13-MEM-01-* rows pass).
Without fix: 15 passed, 1 failed (V13-MEM-01-B fails: "expected bit 1
                                  = 1, got 0x00").
```

**Commit:** TBD (added in the same commit as this report).

## Areas scrutinized — no findings

The following areas were checked exhaustively and confirmed
VHDL-faithful (or already covered by prior pass fixes):

- **NR 0x50..0x57 dispatch (slot mapping)** — engage_legacy_rom_paging_slot
  + set_page paths handle every value range (0xFF sentinel, 0xE0-0xFE
  high-page → slot 0/1 legacy ROM fallback, 0x00-0xDF normal RAM
  mapping). VHDL :4686-4699 nr_mmu_we semantics + :3037-3057 SRAM
  arbiter precedence both correctly modelled. nr_mmu_[] verbatim
  preserved across NR 0x8C / set_machine_type per Pass-12 V12-MEM-01.
- **NR 0x8C alt-ROM control + lo→hi nibble copy on reset** —
  mmu.cpp:104-107 mirrors VHDL :2253-2256 exactly. Per-slot refresh on
  NR 0x8C write only touches read_only_=true slots, preserving NR
  0x50/0x51 RAM mappings (V12-MEM-01 fix path).
- **NR 0x8E unified paging** — `write_nr_8e()` correctly
  decomposes/distributes bits to port_7ffd / port_dffd / port_1ffd
  per VHDL :3662-3734, suppresses MMU6/7 rebuild on bit 3 = 0 path
  (VHDL :3814 port_memory_ram_change_dly), and correctly arbitrates
  the special-mode entry / exit transitions via apply_paging_update_().
- **NR 0x8F mapping mode (Pentagon-512 / Pentagon-1024)** — survives
  reset (VHDL :3787-3794 has no reset clause), `effective_paging_locked`
  composes the full :3769 expression including pentagon_1024_en
  override.
- **NR 0x82/0x83/0x84/0x85 internal_port_enable gates** — port 0x7FFD
  (NR 0x82 b1), port 0xDFFD (NR 0x82 b2), port 0x1FFD (NR 0x82 b3),
  port 0xEFF7 (NR 0x85 b2) all correctly gated.
- **NR 0x12 / NR 0x13 (Layer 2 active/shadow bank)** — both push to
  Mmu's `l2_bank_` / `l2_shadow_bank_` mirrors via dedicated setters;
  port 0x123B write also re-pushes both on every cycle.
- **port 0x7FFD / 0x1FFD / 0xDFFD / 0xEFF7** — `effective_paging_locked()`
  gate, port_memory_change_dly fan-out via `apply_paging_update_()`,
  +3 special-paging table at apply_plus3_special_paging_(), special-mode
  exit revert at revert_slots_2_to_5_post_special_(). All match VHDL
  :3650-3742, :4623-4684 line-for-line.
- **port_eff7_reg_3 RAM-at-0x0000 override** — fires only on
  port_memory_change_dly path (VHDL :4636-4644); NR 0x50/0x51 = 0xFF
  with EFF7(3)=1 correctly leaves slot 0/1 in legacy ROM (V4 fix path).
- **NR 0x08 bit 7 unlock_paging** — clears port_7ffd_(5) AND
  paging_locked_ (Verify3-memory class-(a) fix); does NOT trigger
  port_memory_change_dly (VHDL :3813 — nr_08_we absent from OR-list).
- **Boot ROM overlay** — `set_boot_rom` materialises 8 KB via
  zero-pad/truncate per VHDL :3199-3204; `boot_rom_en_` re-enabled on
  reset only when nr_03_config_mode='1' (VHDL :5109-5111); cleared
  unconditionally on NR 0x03 write (VHDL :5121-5122).
- **Save/load state** — every persisted member field round-trips
  (`slots_[8]`, `nr_mmu_[8]`, `read_only_[8]`, paging_locked,
  port_7ffd, port_1ffd, port_1ffd_special_old, port_dffd_reg,
  port_dffd_reg_6, port_eff7_2/3, nr_8c, nr_8f_mode, machine_type,
  l2_*, config_mode, nr_04_romram_bank, rom_in_sram, boot_rom_en,
  contention_disabled, p3_floating_bus_dat, slot_contended_[4]).
  Pass-12 fixes V12-MEM-02 (ContentionModel state push on load) and
  V12-MEM-03 (machine_type → contention rebuild on load) are in place
  and functioning. Boot ROM data buffer (`boot_rom_buf_`) is
  intentionally not serialised — restored externally via
  `mmu_.set_boot_rom()` from the Emulator-owned `boot_rom_` vector.
- **Per-machine differences (sram_rom/sram_rom3 selection)** —
  `current_sram_rom()` and `sram_rom3()` correctly branch on
  `machine_type_` per VHDL :2981-3008: 48K hardwires sram_rom=0 /
  sram_rom3=1; +3 uses 2-bit port_1ffd_rom; ZXN/128K share the 1-bit
  `port_1ffd_rom(0)` selector. Altrom-lock overrides applied
  symmetrically per machine type (Verify7-memory class-(a) +
  Verify8-memory class-(a) fixes).
- **`mmu_A21_A13(8)` precedence (slots 0/1 with high-page MMU
  values)** — `rebuild_ptr` correctly routes high-page (0xE0-0xFF)
  values on slots 0/1 to legacy-ROM (sram_rom-derived) per VHDL
  :3037/:3052; high-page values on slots 2-7 nullify
  read_ptr_/write_ptr_ for inactive-slot floating-bus behaviour per
  VHDL :3060-3061.
- **+3 special paging table + special-mode exit revert** — VHDL
  :4623-4684 fully modelled; port_1ffd_special_old captures the
  prior special state at every paging trigger.
- **Boot ROM size mismatch tolerance** — set_boot_rom zero-pads /
  truncates any input size to exactly 8 KB so `addr & 0x1FFF` always
  resolves into a valid offset (VHDL bootrom_mod is hardwired to
  cpu_a(12:0), G140/G157).
- **Multiface overlay precedence** — slot 0 access with
  `mf_overlay_active_()` correctly serves MF ROM (low half) / RAM
  (high half) from the dedicated buffers (VHDL :3028-3035, MF
  precedence above DivMMC + Layer 2 + ROMCS).
- **DivMMC overlay slot 0/1 read/write override** — `divmmc_read` /
  `divmmc_write` correctly dispatched ABOVE the config_mode SRAM
  routing per VHDL :3084 arbiter precedence.
- **mem_active_page contention decode** — `mem_contend_for_()` uses
  the LIVE `nr_mmu_[]` MMU<i> register value (Verify6-memory
  class-(a) fix), not the physical SRAM page; high pages 0x10..0xFF
  never contend; per-machine bank decode (48K bank-5-only / 128K odd
  banks / +3 banks ≥ 4) all match VHDL :4489-4493.
- **p3_floating_bus_dat latch** — captures `cpu_di`/`cpu_do` on every
  contended access via per-page mem_contend decode (Verify9-memory
  class-(c)→class-(a) fix), independent of `i_contention_en` gate
  (VHDL :4498-4509 keys only on mem_contend AND mreq_n).
- **NR 0x04 Issue-2 mask (`'0' & nr_wr_dat(6:0)`)** — emulator.cpp:2071
  applies the 7-bit mask matching VHDL gen_romram_234 :5717.
- **Layer 2 read-/write-over hot path** — `l2_overlay_active_for()`
  correctly models VHDL :3043,:3050,:3057 (low-half always enabled,
  non-MF) and :3065 (high-half enabled only when seg="11"); offset_pre
  computation matches VHDL :2966.
- **Contention LUT rebuild on machine-type change** —
  `contention_.rebuild_for_type()` fires on NR 0x03 commit (VHDL
  :5145 path), preserving dynamic gate state (cpu_speed,
  contention_disable shadow/effective, port_7ffd_io_en).

## Summary

| Class | Count |
|-------|-------|
| (a) — VHDL-faithful fix landed | 1 |
| (b) — minor VHDL deviation, no boot impact | 0 |
| (c) — semantically equivalent, low risk | 0 |
| (d) — architectural escalation | 0 |
| **Total findings** | **1** |

## Tests

Release-mode build: `cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`.

| Suite | Pre-fix | Post-fix |
|-------|---------|----------|
| `ctest` | 38/38 PASS | 38/38 PASS |
| FUSE Z80 opcode | 1356/1356 PASS | 1356/1356 PASS |
| `mmu_integration_test` | 11/11 PASS | 16/16 PASS (5 new V13-MEM-01-* rows) |

Discriminative verification of V13-MEM-01-B: temporarily reverted the
`mmu_.set_l2_enable` line in the NR 0x69 handler — `mmu_integration_test`
reported `FAIL V13-MEM-01-B` with detail `[expected bit 1 = 1, got 0x00
(Mmu::l2_enable_ stale after NR 0x69 fan-out)]`. Restoring the fix
recovers `Total: 16 Passed: 16 Failed: 0`.
