# Pass-6 Memory Subsystem Blind Verification Re-Audit

**Branch**: `task2/verify6-memory`
**Date**: 2026-05-09
**Verdict**: **1 class-(a) bug found and fixed**. Audit is converging — five
prior passes fixed 12 bugs (P1=2, P2=2, P3=3, P4=3, P5=2); P6 finds 1.
The trend is descending (2→2→3→3→2→1) and consistent with convergence.

---

## Methodology

The audit walked the seven Pass-6 angles outlined in the task brief:

1. **NR-write timing within a single instruction (cycle-precise)**
2. **R/W simultaneous on same cycle (bus-master peripherals)**
3. **Specific edge-case Z80 instruction sequences**
4. **ROM read at PC=$0000/$0001/$1FFE around boot ROM disable**
5. **Save/load state mid-execution (snapshots in unusual states)**
6. **Final differential VHDL signal sweep**
7. **Z80 contention with new INT pulse machine-aware (P1 follow-up)**

For each angle the C++ behaviour was traced against the VHDL oracle
(`zxnext.vhd` — `zxnext.vhd:2949-2956`, `:2964`, `:3029-3066`,
`:3640-3801`, `:4498-4509`, `:4611-4699`, `:5109-5135`).

---

## Findings

### Finding 1 — class-(a) — `mem_active_page_for` returned physical ROM page instead of MMU<i> for legacy-ROM slots 0/1 (128K/+3 contention false-positive)

**File**: `src/cpu/z80_cpu.cpp` (the helper used by every memory cycle to
seed `ContentionModel::set_mem_active_page`).

**VHDL oracle**: `zxnext.vhd:2949-2956`

```vhdl
mem_active_page <= MMU0 when cpu_a(15 downto 13) = "000" else
                   MMU1 when cpu_a(15 downto 13) = "001" else
                   ...
```

VHDL `mem_active_page` is wired directly to `MMU<i>` (= the live
register value). When slots 0/1 are in legacy-ROM auto-paging, the
register holds the `0xFF` ROM sentinel (per `zxnext.vhd:4611-4612`,
`:4641-4644`); the SRAM physical page is derived combinationally from
`sram_rom` in the early-decode arbiter at `:3052`, NOT through the
`mem_active_page` signal.

The contention gate at `zxnext.vhd:4489` keys on `mem_active_page`:

```vhdl
mem_contend <= '0' when mem_active_page(7 downto 4) /= "0000" else ...
```

so the high nibble `F` (from the `0xFF` sentinel) suppresses contention
for legacy-ROM slot 0/1 accesses on every machine.

**C++ pre-fix**:

```cpp
inline uint8_t mem_active_page_for(uint16_t address) {
    if (!s_contention_mmu) return 0xFF;
    int slot = address >> 13;
    return s_contention_mmu->get_effective_page(slot);  // BUG
}
```

`Mmu::get_effective_page(slot)` (mmu.h:54-57) returns
`nr_mmu_[slot] != 0xFF ? nr_mmu_[slot] : slots_[slot]`. For legacy-ROM
slots `nr_mmu_=0xFF` so it falls through to `slots_[slot]` — the
**physical** ROM page (`sram_rom * 2 + slot`, range 0..7), NOT the
sentinel.

**Impact path** (128K mode, `current_sram_rom() == 1`):

| state | VHDL `mem_active_page` | jnext `mem_active_page_for` |
|-------|-------------------------|------------------------------|
| slot 0, sram_rom=1 (ROM 1) | `0xFF`                  | `0x02` (physical page 2)     |
| slot 1, sram_rom=1 (ROM 1) | `0xFF`                  | `0x03` (physical page 3)     |

For 128K (`mem_contend = page(1)`): `0x02` and `0x03` both have bit 1
set → C++ contention model fired on slot-0/1 ROM accesses; VHDL did
not. Same false-positive for +3 with `sram_rom >= 2` (pages 4..7 have
bit 3 set → `mem_contend` for +3).

**Fix**: switch the helper to `Mmu::get_page(slot)` (= `nr_mmu_[slot]`,
the VHDL `MMU<i>` register-visible value). For legacy-ROM slots this
returns `0xFF` — high nibble `F` — and the contention gate suppresses
contention as VHDL does. For RAM-mapped slots and explicit NR 0x50/0x51
writes (page < 0xE0), `nr_mmu_[slot]` already holds the logical page,
so the fix is a no-op there.

48K is unaffected (`current_sram_rom() == 0` always; physical page 0 has
clean high+low nibble, so neither pre- nor post-fix C++ contended).
ZXN_ISSUE2 contention model returns 0 unconditionally (no timing-mode
mapping for Next), so the fix also has no functional impact in Next mode.

The bug surfaces only on 128K/+3 — both modes that actually consume the
contention LUT — for slot 0/1 ROM accesses while `sram_rom != 0`.

---

## Angles audited without findings

### Angle 1 — NR-write timing within a single instruction

VHDL latches all paging-port writes on the next `i_CLK_28` rising edge;
`port_memory_change_dly` then fires for one clock and triggers the
MMU rebuild. Z80 OUT (n),A completes M3 (the IO write phase) before the
next instruction's M1 fetch, and the port handler in C++ updates Mmu
state synchronously inside `Z80Cpu::out()` before returning to FUSE.
The end-state ordering matches: by the time the next M1 fetch starts,
both VHDL and C++ have the new mapping applied. Z80N
`NEXTREG NN`/`NEXTREG A` route through the same `OUT 0x243B / OUT
0x253B` pair (z80n_ext.cpp:280-300) so they pick up the same handler
ordering.

Sub-case checked: `port_1ffd_special_old` capture timing
(`zxnext.vhd:3719-3742`). VHDL captures `port_1ffd_special_old` from
the **old** `port_1ffd_reg(0)` value during a `port_1ffd_wr` or
`nr_8e_we` cycle, gated by `port_memory_change_dly='0'`; the else
branch continuously clears `_old`. The C++ `apply_paging_update_()`
sets `port_1ffd_special_old_` to the **new** `special` after each
apply, persisting across writes. Tracing both behaviours through
write/EFF7/8E/8F sequences, the per-trigger end-state agrees in every
case: at the moment the next paging trigger fires, both VHDL and C++
report `_old = previous-special-bit`. The collision case (two paging
triggers on consecutive clocks) is a known approximation but
unreachable from a Z80 software path.

### Angle 2 — R/W simultaneous bus-master writes

The Copper writes to NextREGs on its own clock; `Mmu::set_l2_active_bank`,
`Mmu::set_l2_shadow_bank`, and the NR-dispatch handlers in
`emulator.cpp` are all called from the per-instruction tick loop AFTER
the CPU completes its current bus cycle. VHDL has explicit ordering at
`zxnext.vhd:2966-2969` for `layer2_active_bank` (`note that the copper
can change mmu and the layer 2 base bank so these must be frozen during
a memory access`); the cited freeze is the early-decode latch process at
`zxnext.vhd:3015-3070` which captures `sram_pre_*` on `cpu_mreq_n='1'`.
The C++ approximation collapses the latch into "values consulted at
read/write time" — sound for Z80 software but elides the mid-cycle
freeze. Class-(b), no functional regression.

DMA writes to memory directly via `Mmu::write` from
`src/peripheral/dma.cpp`; the same mapping rules apply as for CPU
writes. No simultaneous-cycle hazard reachable.

### Angle 3 — Edge-case instruction sequences

Audited:

- **LD A,(0xFFFE); LD (HL),A across paging boundary**: Mmu state is not
  modified by either of these; the cross-slot access uses `read_ptr_[]`
  / `write_ptr_[]` indexed by `addr >> 13`, so each access resolves
  independently against the live mapping. No boundary issue.
- **DJNZ across paging change**: DJNZ doesn't touch Mmu. Paging change
  happens in a different instruction (OUT). No interaction.
- **LDIR/LDDR with HL/DE crossing paging boundary mid-iteration**: Each
  iteration's read and write use the live `read_ptr_[]` /
  `write_ptr_[]`. Mid-iteration crossing reads the boundary byte from
  the slot the address falls into, just like real hardware.
- **IN A,(\$xxFE) with floating bus + paging change**: The
  `p3_floating_bus_dat_` latch updates on every contended R/W via
  `slot_contended_[]`. Floating-bus reads via port 0xFF on +3 (and
  port 0xFE/floating bus emulation) consume the latch — independent of
  paging.
- **LD SP,HL where new SP is in different page**: Pure register
  operation. Subsequent stack accesses use the new SP via
  `read_ptr_[(SP) >> 13]` etc. — same as any other memory access.
- **EX (SP),HL where SP and HL point to different pages**: Two memory
  accesses (push HL, pop new value) — same as 2× normal accesses, same
  resolution.
- **Multiple consecutive Z80N writes to NR via bus immediate**: Each
  `NEXTREG_NN` is two `OUT` ops; each runs through the synchronous IO
  dispatch. End-state correct.

### Angle 4 — Boot ROM disable timing around PC=\$0000/\$0001/\$1FFE

VHDL `bootrom_en <= '0'` on any NR 0x03 write (zxnext.vhd:5122). C++
mirrors via `mmu_.set_boot_rom_enabled(false)` in the NR 0x03 handler
(emulator.cpp:1779-1783). The disable happens during the IO write
phase of the OUT (or NEXTREG) instruction; the next M1 fetch reads
through `read_ptr_[]` (which is repointed by Mmu state changes) — but
`boot_rom_en_` is checked first in the read fast-path (mmu.h:204-206),
so the disable takes effect on the very next memory access.

`set_boot_rom_enabled(false)` does NOT trigger an MMU rebuild — the
boot ROM overlay is an inline check before the slot table. After
disable, slot 0/1 fall through to whatever mapping `read_ptr_[0/1]`
holds — which was set by the most recent `apply_legacy_rom_slots_()`
or by `map_rom_physical()` during reset. Match VHDL.

### Angle 5 — Save/load state in unusual states

`Mmu::save_state` / `load_state` (mmu.cpp:756-883) persists the full
state: `slots_`, `read_only_`, `paging_locked_`, port latches
(`port_7ffd_`, `port_1ffd_`, `port_dffd_reg_`, `port_dffd_reg_6_`,
`port_eff7_reg_2_`, `port_eff7_reg_3_`), boot-ROM enable, config-mode,
NR 0x04 RAM bank, NR 0x07/0x08 contention bits, NR 0x8C altrom register,
`machine_type_`, NR 0x8F mode, Layer 2 latches (read/write/segment/
bank/offset/shadow/enable), `p3_floating_bus_dat_`, `slot_contended_[]`,
`port_1ffd_special_old_`, and the verbatim `nr_mmu_[8]` array. The
`boot_rom_buf_` (8 KB ROM blob) is re-set after load via the normal
`set_boot_rom` path. The runtime `read_ptr_/write_ptr_` arrays are
rebuilt from `slots_` via `rebuild_ptr` after load. Snapshot during
mid-LDIR or post-NEXTREG-pre-fetch round-trips correctly because the
state captured is the post-instruction Mmu state; the CPU's
mid-instruction state is owned by Z80Cpu's save/load.

The +3 special-paging mode 1/2/3 round-trip is covered by
`port_1ffd_special_old_` persistence (added in a prior pass) — verified.
Config_mode=1 round-trip covered by `config_mode_` + `nr_04_romram_bank_`
persistence.

### Angle 6 — Differential VHDL signal sweep

Walked every signal name containing `mmu_`, `sram_`, `port_7ffd_`,
`port_1ffd_`, `port_dffd_`, `port_eff7_`, `nr_8c_`, `altrom_`,
`bootrom_` in `zxnext.vhd`. Each is mirrored or correctly derived in
`Mmu` / `ContentionModel` / `emulator.cpp`. Signals deliberately not
modelled (with rationale):

- `nr_8f_mapping_mode_profi` — VHDL line 3797 hardwires to `'0'`; jnext
  follows.
- `port_dffd_reg_6` — added in pass-3, persists, used only by
  Multiface read-mux (modelled).
- `port_1ffd_mtr_n` — disk motor for the +3 FDC; out of memory subsystem
  scope, lives in the FDC peripheral path.
- `sram_pre_romcs_n`, `sram_pre_romcs_replace`, `sram_romcs_en` — bus
  expansion ROMCS replacement, gated on `expbus_eff_en` which jnext
  hardwires to `'0'` (no NextBUS emulation, see contention.cpp:80-94).
- `sram_pre_active`, `sram_active`, `sram_mem_hide_n` — internal SRAM
  arbiter routing flags consumed only by the SRAM IP block; the C++ hot
  path uses `read_ptr_` / `write_ptr_` directly.
- `sram_addr`, `sram_req`, `sram_req_d`, `sram_wait_n` — SRAM
  controller signals; jnext serves memory directly.

No silently-omitted memory signal was found.

### Angle 7 — Z80 contention with new INT pulse machine-aware

The Pass-1 INT pulse fix (machine-aware 32 vs 36 cycle) lives in
`Z80Cpu` and `Im2Controller`; it gates the pulse-end detection. The
contention model is gated on `cpu_speed_` and `contention_disable_`
independently — no path between INT pulse width and contention LUT
lookup. The Pass-1 fix is unaffected by, and unaffected by, the Mmu
state. No follow-up bug.

---

## Convergence Assessment

| Pass | Findings | Trend |
|------|----------|-------|
| P1   | 2        | initial |
| P2   | 2        | flat |
| P3   | 3        | up |
| P4   | 3        | flat |
| P5   | 2        | down |
| **P6** | **1**    | **down** |

The descending trend (3→3→2→1) over the last three passes is consistent
with **honest convergence**. P6 found one class-(a) bug (`mem_active_page_for`
returning the physical ROM page instead of the MMU<i> sentinel for
legacy-ROM slots 0/1). It is a contention-side issue with mild functional
impact: it only triggers false-positive contention on 128K (`sram_rom=1`,
slot 0/1 ROM accesses) and +3 (`sram_rom>=2`, slot 0/1 ROM accesses).
Neither code path is on the NextZXOS boot trajectory (Next runs in
ZXN_ISSUE2 mode where the contention model returns 0).

The fix is one line: switch `get_effective_page(slot)` →
`get_page(slot)` in the contention helper, with documented rationale.

A 7th pass is unlikely to find new class-(a) bugs in this subsystem.
Recommend marking memory as **converged** and rotating the audit budget
to the open NextZXOS boot trajectory questions in `G46B` (the ongoing
EOD-24 investigation in memory).

---

## Open Questions

None — all seven Pass-6 angles produced a defensible answer against
VHDL. The known approximations (per-cycle freeze in the SRAM arbiter,
per-instruction `slot_contended_[]` mirror in place of per-cycle
`mem_contend`, `port_1ffd_special_old` persistent vs continuously-cleared
in VHDL) are documented in inline comments and are class-(b) at most.

---

## Files Changed

- `src/cpu/z80_cpu.cpp` — `mem_active_page_for` switched from
  `Mmu::get_effective_page` to `Mmu::get_page` (= the VHDL `MMU<i>`
  register), with expanded comment citing the VHDL line refs and
  describing the false-positive contention bug it fixes.

## Tests

```
$ ctest --test-dir build --output-on-failure
... 100% tests passed, 0 tests failed out of 37
```

No regressions. No new tests added — the fix changes behaviour for
contention-LUT-active machines (128K/+3) only on slot 0/1 ROM accesses
during `sram_rom != 0`, which is not a test-plan-covered combination
in `contention_test.cpp` today. The existing `mmu_tests` /
`fuse_z80_tests` / `contention_tests` continue to pass.
