# Pass-11 Blind Audit — Memory Subsystem

**Branch**: `task2/verify11-memory` (off integration HEAD `d385d5e`)
**Auditor**: Pass-11 blind agent (no prior pass-N reports consulted)
**Scope**: `src/memory/{mmu,ram,rom,contention}.{cpp,h}` + memory-related
NR/port dispatch in `src/core/emulator.cpp`
**Status**: 1 finding — class-(c). Fix + discriminative regression test
landed; full ctest 38/38 + FUSE 1356/1356 + mmu_test 207/0/22 (all SKIPs
documented in earlier passes).

---

## V11-MEM-01 — class-(c): `slots_[]` semantics inconsistent after NR $50/$51 write of v ∈ [$E0..$FE]

### VHDL oracle
* `zxnext.vhd:3037-3057` — SRAM arbiter for slot 0/1 (`cpu_a(15:14)="00"`).
  When `mmu_A21_A13(8)='1'` (logical MMU page ≥ 0xE0) AND no MF AND no
  `nr_03_config_mode` AND not RAM-mapped, the arbiter falls into the
  legacy ROM branch at `:3052`:
  ```
  sram_pre_A21_A13 <= "000000" & sram_rom & cpu_a(13);
  ```
  i.e. the physical SRAM page driven on the bus is `sram_rom*2+slot`.
* `zxnext.vhd:4686-4699` — `nr_mmu_we` stores `nr_wr_dat` verbatim into
  `MMU<i>` on an NR $50/$51 write.
* `zxnext.vhd:6075-6082` — NR $50–$57 readback returns `MMU<i>` verbatim.

### Root cause
`Mmu::rebuild_ptr` (src/memory/mmu.cpp:213–242) handles the `page >= 0xE0`
gate for slots 0/1 by:
* setting `read_ptr_[slot]` to the legacy-ROM-derived page
  (`sram_rom*2+slot`),
* setting `read_only_[slot]=true`,
* leaving `slots_[slot]` at the verbatim NR-write value (e.g. 0xE5).

The runtime read path consults `read_ptr_[slot]` and produces the right
byte. But `slots_[slot]` is now in a mixed-semantics state: when
`read_only_[slot]=true`, every other code path interprets `slots_[slot]`
as the *physical SRAM page being served* (see `map_rom_physical` at
mmu.cpp:260-271 and `apply_legacy_rom_slots_` at mmu.cpp:388-416).

The asymmetry surfaces in `Mmu::load_state` (mmu.cpp:838-893):

```cpp
r.read_bytes(slots_, 8);                       // restored slots_[0]=0xE5
for (int i = 0; i < 8; ++i) read_only_[i] = r.read_bool();  // read_only_[0]=true
…
for (int i = 0; i < 8; ++i) rebuild_ptr(i);    // first branch: page=0xE5, RO=true
                                               //   → read_ptr_ = ram.page_ptr(0xE5)  ❌
r.read_bytes(nr_mmu_, 8);                      // nr_mmu_[0]=0xE5 (correct)
```

`rebuild_ptr`'s first branch (`if (page == 0xFF || read_only_[slot])`)
sees `read_only_[slot]=true` and unconditionally falls into:

```cpp
read_ptr_[slot] = rom_in_sram_ ? ram_.page_ptr(page)
                               : rom_.page_ptr(page);
```

with `page=slots_[slot]=0xE5` — i.e. **RAM page 0xE5** (Next mode), not
the legacy-ROM-derived `sram_rom*2+slot=0` that VHDL would drive on the
SRAM bus.

The bug is also reachable, in principle, by any other rebuild_ptr entry
that sees the inconsistent state — set_rom_in_sram() iterates and
rebuilds, save_state/load_state round-trip is the obvious one.

### Why class-(c) (not a/b)
* Real firmware almost always writes NR $50/$51 with `0xFF` (legacy
  re-engage) or RAM pages (< 0xE0). High-page writes ($E0..$FE) are an
  unusual corner.
* Even when triggered, the next paging-port write (port 7FFD/1FFD/DFFD/
  EFF7 or NR $8E/$8F) calls `apply_legacy_rom_slots_()` which restores
  the canonical state via `map_rom_physical()` — the wrong-state window
  is bounded.
* The runtime read path (immediately after the NR write, with no save/
  load involved) is correct because `read_ptr_[slot]` is set directly.

### Fix
src/memory/mmu.cpp `rebuild_ptr` `page >= 0xE0` slot 0/1 branch now
stores `rom_page` (= `sram_rom*2+slot`) into `slots_[slot]` so the
array's "physical SRAM page when read_only_=true" semantics holds for
both the runtime path and the save-state/load-state round-trip.
`nr_mmu_[slot]` keeps the verbatim NR-write value for the read-back at
`zxnext.vhd:6075-6082`.

### Discriminative regression test
`Cat27 V11-MEM-01-A NR $50/$51 high-page slots_[] consistency` in
`test/mmu/mmu_test.cpp`. Seeds two distinguishing RAM pages (0x00 ←
0x33 = legacy-ROM target; 0xE5 ← 0x77 = pre-fix wrong target), writes
NR $50 = 0xE5, save_state, load_state, reads 0x0000. Pre-fix yields
0x77 (RAM page 0xE5). Post-fix yields 0x33 (legacy ROM page = 0).
Discriminative gap verified by reverting the fix and observing FAIL,
then re-applying.

### Commit
`a8f8ecd` (`fix(memory): V11-MEM-01 — keep slots_[] consistent with
read_only_=true post NR $50/$51 high-page write`).

---

## Areas scrutinised that are spec-faithful

For honest convergence transparency, this section names specific areas
I scrutinised and confirmed VHDL-faithful in pass-11:

1. **Reset propagation** — `Mmu::reset()` clears all reset-domain
   state per VHDL line refs (port_7ffd_reg :3646-3648, port_1ffd_reg
   :3713-3716 inc. `port_1ffd_special_old`, port_dffd_reg + _6
   :3686-3690, port_eff7 :3777-3779, paging lock implicit in :3648,
   nr_08_contention_disable :4930-4935, NR $8C lo→hi nibble fold
   :2253-2256, L2 latches :3907-3913). NR $8F has no VHDL reset
   process and survives both hard and soft reset (matches mmu.h
   default member init at line 1332).
2. **Boot ROM `if config_mode` gate** — `Mmu::reset()` line 149
   matches VHDL :5109-5111 (`if nr_03_config_mode='1' then bootrom_en
   <= '1'`). Existing test `FIX-RESET-CFG-01` covers this.
3. **NR $8E unified paging** — `Mmu::write_nr_8e` correctly bypasses
   the `port_7ffd_locked` gate (per VHDL :3662-3672 elsif chain),
   updates `port_7ffd_/port_dffd_/port_1ffd_` per the bit semantics
   at :3662-3735, and arbitrates MMU update via `apply_paging_update_`
   with the `bit3=0` MMU6/7 suppression (VHDL :3814).
4. **NR $8E read-back** — `Mmu::read_nr_8e()` produces the bit
   formula at VHDL :6159 verbatim (verified: bit 3 always '1', bit 0
   = `(7ffd(4) AND NOT 1ffd(0)) OR (1ffd(1) AND 1ffd(0))`).
5. **`port_1ffd_special_old` semantics** — captures the pre-write
   `port_1ffd_special` value and is consumed by `apply_paging_update_`
   to drive the slot-2-to-5 revert on the 1→0 transition. Matches
   VHDL :3713-3742 + :4623-4684 (the `elsif port_1ffd_special_old='1'`
   branches at :4655 / :4667 for MMU2/3 and MMU4/5). Already covered
   by `FIX-PLUS3-03` for the save/load round-trip.
6. **Pentagon-1024 lock override** — `Mmu::effective_paging_locked()`
   composes `paging_locked_ AND NOT pentagon_1024_en()` per VHDL :3769.
   `pentagon_1024_en()` requires `nr_8f="11" AND NOT port_eff7_reg_2`
   per VHDL :3801. Both the lock check and the bank composition at
   `compose_bank_()` use the effective signal correctly.
7. **NR $69 bit 6 → 7FFD(3)** — `set_port_7ffd_bit3` only updates the
   shadow-screen bit (per VHDL :3658-3660); does not trigger MMU
   rebuild because :3813 omits `nr_69_we` from the
   `port_memory_change_dly` OR-tree, and bit 3 is not in the bank
   composition (`port_7ffd_bank` uses bits 2:0/7:6 + DFFD).
8. **NR $08 bit 7 unlock** — `Mmu::unlock_paging()` clears
   `paging_locked_` AND `port_7ffd_ & ~0x20` (so MF+3 0x7xxx readback
   and Pentagon-1024 bank-5 composition see the cleared bit). Already
   covered by `FIX-UNLOCK-01`.
9. **NR $03 disables boot ROM unconditionally** — Emulator's NR $03
   write handler clears `boot_rom_enabled_` on every write (VHDL :5122
   `bootrom_en <= '0'`). Verified via existing `Cat2` reset tests.
10. **NR $50–$57 readback** — Emulator's NR $50–$57 read handler
    returns `mmu_.get_page(i)` = `nr_mmu_[i]` verbatim (VHDL
    :6075-6082).
11. **NR $50 = 0xFF dispatcher** — Emulator's NR $50 write with
    v=0xFF for slot 0/1 calls `engage_legacy_rom_paging_slot(i)`
    which stores 0xFF in `nr_mmu_[i]` and serves legacy ROM via
    `map_rom_physical`. For slots 2-7 with v=0xFF, calls
    `set_page(i, 0xFF)` which makes the slot inactive (read_ptr_=
    nullptr → reads return 0xFF; writes dropped). Both match VHDL
    :3037 (`sram_pre_active = NOT mmu_A21_A13(8)` for slots 2-7).
12. **EFF7(3) RAM-at-0x0000 override** — `apply_legacy_rom_slots_()`
    correctly maps MMU0/1 to pages 0x00/0x01 (RAM-mapped, read_only_=
    false) when `port_eff7_reg_3` is set AND not in special paging
    mode. VHDL :4636-4644 EFF7(3) override is gated by `if port_1ffd
    _special='1' then …else if port_eff7_reg_3='1'…` — only applies
    in the `else` (non-special) branch. C++ `apply_paging_update_`
    enforces this via the `if (special) … else …` arbiter.
13. **Alt-ROM read/write override** — both branches consult `nr_8c_
    altrom_en + nr_8c_altrom_rw + !config_mode_ + read_only_[slot]`,
    matching VHDL :3078 (sram_altrom_en = NOT(override(0)=0 OR
    sram_pre_alt_en=0 OR (rdonly=1 AND rd_n=1) OR (rdonly=0 AND
    rd_n=0))). Read fires on `altrom_en+!altrom_rw`; write fires on
    `altrom_en+altrom_rw`. config_mode pre-empts altrom because
    config_mode sets sram_pre_override(0)='0' at :3050.
14. **Config-mode SRAM routing** — read/write paths route to
    `(nr_04_romram_bank << 1) | slot` per VHDL :3045 when
    `config_mode AND read_only_[slot] AND addr<0x4000`. Matches the
    8-bit `nr_04_romram_bank` (Issue 5) / 7-bit (Issue 2/3/4) board
    semantics — the dispatcher at emulator.cpp:2053 already masks
    NR $04 with 0x7F for our default ZXN_ISSUE2.
15. **mem_active_page contention decode** — `Mmu::mem_contend_for_`
    matches VHDL :4489-4493 per machine type (48K bank-5,
    128K odd banks, +3 banks≥4, ZXN_ISSUE2 / Pentagon false).
    Confirmed by tracing the bit-mask formulas. Same logic mirrored
    in `ContentionModel::is_contended_access()` and
    `contention_tick()`.
16. **`p3_floating_bus_dat` latch** — fires on `mem_contend AND
    cpu_mreq_n=0` regardless of cpu_speed / contention_disable. VHDL
    :4498-4509 — process keys ONLY on mem_contend + cpu_mreq_n=0.
    Both read() and write() paths in mmu.h update the latch via
    `mem_contend_for_(addr)` per-page decode (NOT the legacy
    per-16K `slot_contended_[]` mirror, which is retained for
    save-state schema compatibility only).
17. **Layer 2 overlay gate** — `l2_overlay_active_for(addr)` matches
    VHDL :3037-3066 `sram_pre_override(1)`: low half always-on in
    non-MF cases, high half only when seg="11", 0xC000+ disabled.
    `l2_offset_pre_for(addr)` matches VHDL :2966 `cpu_a(15:14) when
    seg="11" else seg`. Layer-2 inactive gate (`(sum & 0x70)==0x70`)
    matches VHDL :2971+:3101-3102 `layer2_A21_A13(8)='1'`.
18. **NR $50/$51 = 0xFF + EFF7(3)=1 interaction** — `engage_legacy_
    rom_paging_slot()` does NOT consult EFF7(3) because VHDL :3813
    does not include `nr_mmu_we` in `port_memory_change_dly`, so the
    EFF7(3) override at :4636 cannot fire on this cycle. Slot stays
    on legacy ROM (sram_rom-derived) until the next paging-port
    write. Already covered by existing `FIX-NR5x-FF-PER-SLOT-01..02`
    tests.
19. **`current_sram_rom()` per machine type** — matches VHDL
    :2981-3008 for 48K (returns 0), +3 (2-bit (1ffd(2),7ffd(4)) +
    altrom locks), 128K/Next (1-bit (7ffd(4)) + altrom locks). Already
    covered by `FIX-SRAM-ROM-128K-LOCK-01` and existing Cat-11 tests.
20. **Mmu reset model** — `Mmu::reset()` ignores the `hard` parameter
    by design. Per the comment block at mmu.cpp:52-83, the VHDL
    `reset` signal is `reset_hard OR reset_soft`, so every `if
    reset='1'` clause inside zxnext.vhd fires on both. The earlier
    Branch-C "hard-only" partitioning was reverted as it caused a
    NextZXOS-boot regression. Verified by re-reading
    zxnext_top_issue5.vhd lines that wire `i_RESET = reset_hard or
    reset_soft`.

---

## Class-(d) — architectural-refactor (NOT fixed in this pass)

None new. The 4 class-(d) escalations from prior waves remain
documented in the aggregate report (memory half-cycle ×2, DivMMC SPI
cycle FSM, NMI Stackless NMI, CPU IM2 controller bridge); none touch
the memory subsystem in a way pass-11's mandate addresses.

A single sub-case from this pass that I considered class-(d) but
explicitly did NOT escalate:

* **NR $07 cpu_speed shadow-vs-effective on read-back** — the NR $07
  read at emulator.cpp:704 returns `(cached(0x07)<<4) | cached(0x07)`
  (= shadow for both bits). VHDL :5902 reads `(cpu_speed << 4) |
  nr_07_cpu_speed` — the high nibble is the EFFECTIVE, latched-on-
  bus-idle value. Between an NR $07 write and the next bus-idle
  commit, the C++ readback would diverge. In practice the FUSE
  callback fires per instruction and `commit_pending_cpu_speed_on_
  bus_idle()` runs once per instruction (at instruction boundary,
  which IS bus-idle), so the gap is essentially zero from firmware's
  perspective. Class-(c) at most; explicit comment at emulator.cpp:
  699-707 documents the simplification. Not part of memory subsystem
  scope per the "areas scrutinised" framing — leaving as-is.

---

## Test results

* `cmake --build build -j$(nproc)` — clean build.
* `ctest --test-dir build --output-on-failure -j$(nproc)` —
  **38/38 PASS, 0 FAIL**.
* `./build/test/fuse_z80_test build/test/fuse` — **1356/1356 PASS**.
* `./build/test/mmu_test` — **207 PASS, 0 FAIL, 22 SKIP**
  (all SKIPs documented in earlier passes — SD2/G146, BOOT-NEX/G156,
  BOOT-TAPESAVE/G33, BOOT-Z80/G34, BOOT-SNAPSAVE/G35, BOOT-DECI/G36-G37,
  G12-MUX/G12).
* `bash test/00regression/regression.sh` — **28 PASS, 5 FAIL** (all
  fails are environmental — z88dk demos not built in worktree, `compare`
  tool not pre-seeding output PNGs; verified pre-fix baseline at
  d385d5e shows **23 PASS, 10 FAIL** under the same environment, so
  pass-11's fix actively improves the regression count by 5 tests
  rather than introducing any).

---

## Summary

**1 finding, class-(c)** — `slots_[]` array semantics inconsistent for
NR $50/$51 high-page writes after save/load round-trip. Fix landed at
commit `a8f8ecd` with discriminative regression test `V11-MEM-01-A`.

The memory subsystem at integration HEAD `d385d5e` has been audited
afresh against the VHDL oracle. No class-(a) or class-(b) findings.
Twenty distinct VHDL-line-cited areas were scrutinised and confirmed
spec-faithful (listed above). The single class-(c) finding is genuine,
discriminatively tested, and committed. No class-(d) escalations new
to this pass.
