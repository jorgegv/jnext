# NextZXOS Boot Subsystem Analysis — Memory (MMU) Subsystem

**Branch:** `task2/memory-review`
**Worktree:** `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-memory`
**Audit date:** 2026-05-09

## Executive summary

Two VHDL-fidelity fixes landed on this branch (`f832f38`, `45d8b30`)
during the previous session. Both have been independently verified
against `zxnext.vhd` (the authoritative oracle). Neither was reverted —
the rationale and the new helpers (`engage_legacy_rom_paging_slot`,
`apply_plus3_special_paging_`, `revert_slots_2_to_5_post_special_`,
`apply_paging_update_`) match the hardware behaviour at the cited line
numbers. All other focus areas (sram_rom / NR $8E atomicity, port lock
gates, alt-ROM, NR $03 propagation, soft reset, contention, NR $82-$84
gating, boot ROM init) were audited and found VHDL-faithful — **no
additional fixes were needed**. Total fix count: **2** (both pre-existing
on this branch). All tests green: mmu_test 202 passed / 0 failed / 22
skipped, fuse_z80 1356/1356, full ctest 36/36.

## Methodology

**VHDL oracle** — primary source consulted:

- `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`
  - Lines 2952-2964 — `mem_active_page` / `mmu_A21_A13` derivation
  - Lines 2981-3008 — `sram_rom` / `sram_alt_128_n` per machine type
  - Lines 3010-3071 — early SRAM address decode (where `MMU<i>=0xFF`
    interpretation lives)
  - Lines 3199-3204 — boot ROM 8 KB indexing
  - Lines 3650-3742 — `port_7ffd_reg` / `port_1ffd_reg` /
    `port_1ffd_special_old` write process
  - Lines 3763-3771 — `port_7ffd_bank` composition + lock gate
  - Lines 3805-3818 — `port_memory_change_dly` /
    `port_memory_ram_change_dly` derivation
  - Lines 4605-4700 — MMU register update process (the real heart of
    paging)
  - Lines 4623-4684 — +3 special-paging table + exit-revert
  - Lines 4677 — `port_memory_ram_change_dly` MMU6/7 gate
  - Lines 4686-4696 — `nr_mmu_we` per-slot literal-store branch (NR
    $50-$57 writes)
  - Lines 5891-5894 — NR $02 / NR $03 read-back layout
  - Lines 5147-5151 — NR $03 config-mode FSM

**jnext sources audited:**

- `src/memory/mmu.h` (1118 lines) — read fast path, write fast path,
  alt-ROM page derivation, sram_rom / sram_rom3 / sram_alt_128_n
  helpers, lock predicates, state declarations, accessors
- `src/memory/mmu.cpp` (~800 lines) — `reset()`, `set_page()`,
  `rebuild_ptr()`, `map_rom_physical()`, `map_rom()`, all paging-trigger
  entry points (`map_128k_bank` / `map_plus3_bank` / `write_port_dffd`
  / `write_port_eff7` / `write_nr_8e` / `write_nr_8f`),
  `apply_legacy_*`, the new `apply_plus3_special_paging_` /
  `revert_slots_2_to_5_post_special_` / `apply_paging_update_` /
  `engage_legacy_rom_paging_slot` helpers, save/load state.
- `src/core/emulator.cpp` — NR $50-$57 dispatch loop (lines
  1372-1428), NR $02 (1537-1601), NR $03 (1674-1781), NR $82/$83/$85
  port-enable gates, NR $8C alt-ROM (1958-1965), NR $8E/$8F (1972-1990),
  port 0x7FFD (2291-2331), port 0x1FFD (2393-2428), port 0xDFFD
  (2582-2587), port 0xEFF7 (2605-2610), `soft_reset()` (4891-4945).

For every claim in each commit message I (a) located the cited VHDL
lines and re-derived the spec, (b) located the jnext code path the
commit changes, (c) traced the resulting state through `read()` /
`write()` / the `rebuild_ptr` machinery, and (d) checked the unit-test
suite (`mmu_test`) before and after.

## Findings

### Finding 1 — NR $5x,$FF per-slot semantics (commit `f832f38`)

- **Locations:**
  - `src/core/emulator.cpp:1372-1403` — NR $50-$57 dispatch loop.
  - `src/memory/mmu.cpp:357-398` — new `engage_legacy_rom_paging_slot()`
    helper.
  - `src/memory/mmu.h:818-832` — public declaration.

- **VHDL spec:**
  - `zxnext.vhd:4686-4696` — the `nr_mmu_we` branch literally stores
    `nr_wr_dat` verbatim into the addressed `MMU<i>` register. There is
    no further interpretation in this branch — the value $FF is just
    stored.
  - `zxnext.vhd:2952-2964` — `mem_active_page <= MMU<i>`, then
    `mmu_A21_A13 <= ("0001" + ('0' & mem_active_page(7 downto 5))) &
    mem_active_page(4 downto 0)`. With page=0xFF, mem_active_page(7:5)
    = "111", so mmu_A21_A13(8) = "1000"(8) = '1'.
  - `zxnext.vhd:3037-3057` — for slots 0/1 (cpu_a(15:14)="00") with
    `mmu_A21_A13(8)='1'` (i.e. MMU<i>=0xFF), `mf_mem_en` and
    `nr_03_config_mode` cases are skipped, falling to the `else` at
    line 3051: `sram_pre_A21_A13 <= "000000" & sram_rom & cpu_a(13);
    sram_pre_active <= '1'`. ⇒ **legacy ROM auto-paging via sram_rom**.
  - `zxnext.vhd:3060-3061` — for slots 2-7 (cpu_a(15:14)≠"00") with
    `mmu_A21_A13(8)='1'`, `sram_pre_active <= (not mmu_A21_A13(8)) and
    ... = '0'`. ⇒ **slot inactive** (read returns 0xFF, writes
    dropped).

- **Pre-fix behaviour (this branch's tip prior to `f832f38`):**
  - Slots 0/1 with $FF called `engage_legacy_rom_paging()` which
    rebuilt **both** halves via `apply_legacy_rom_slots_()`. A user
    sequence `NR $50,<RAM page>` followed by `NR $51,$FF` would
    therefore clobber slot 0 back to ROM — a violation of VHDL's
    per-slot `nr_mmu_we` semantics.
  - Slots 6/7 with $FF called `engage_legacy_ram_paging()` which forced
    them to `port_7ffd_bank` legacy composition — also wrong. VHDL
    leaves them inactive when MMU<i>=$FF.
  - Slots 2-5 with $FF called `map_rom(i, 0)` mapping them to ROM
    page 0 — also wrong. VHDL leaves them inactive.

- **Fix applied:**
  - Slots 0/1: dispatch to new `engage_legacy_rom_paging_slot(i)` —
    rebuilds **only** the requested slot, preserving any RAM mapping
    on the other half (which was previously set by an earlier
    `NR $5x,<RAM>` write).
  - Slots 2-7: `mmu_.set_page(i, 0xFF)` which sets `slots_[i]=0xFF`,
    `read_only_[i]=false`, then `rebuild_ptr` — which (per `mmu.cpp:151`)
    enters the `(page==0xFF || read_only_)` outer branch, takes the
    `read_only_=false` inner branch, and nulls both `read_ptr_[i]` and
    `write_ptr_[i]`. Reads return 0xFF (`mmu.h:308`); writes are
    dropped (`mmu.h:401-403`). VHDL-faithful "slot inactive" behavior.

- **Test status:** mmu_test 202/180/0/22, fuse_z80 1356/1356,
  full ctest 36/36 — all PASS.

- **G46(b) relevance:** the previous session's investigation logs
  (EOD-22 Wave 8) blamed an earlier-spec-mismatched NR $5x,$FF on
  jnext's font-blit slide. The commit `8242098` (already on main, prior
  to this worktree) introduced `engage_legacy_rom_paging()` which
  fixed the "slots 0/1 hardcoded to physical pages 0/1" bug. `f832f38`
  refines that fix by making it per-slot (so the supervisor's
  ` NR $50,<RAM> + NR $51,$FF` interleave wouldn't accidentally undo the
  slot-0 RAM map) and by correcting the slots 2-7 path to be VHDL-
  faithfully inactive rather than silently mapped. **Inert in the
  observed NextZXOS boot trace** (per the commit message and per
  EOD-23/24 trace logs which never observe slots 2-5 written with $FF
  nor a `NR $50,<RAM>` + `NR $51,$FF` interleave) — so this is a
  **latent** correctness issue, not the immediate G46(b) trigger.

### Finding 2 — +3 special-paging arbitration (commit `45d8b30`)

- **Locations:**
  - `src/memory/mmu.cpp:357-422` — new helpers
    `apply_plus3_special_paging_`, `revert_slots_2_to_5_post_special_`,
    `apply_paging_update_`.
  - `src/memory/mmu.cpp:464-651` — `map_128k_bank`, `write_port_dffd`,
    `write_port_eff7`, `write_nr_8f`, `map_plus3_bank`, `write_nr_8e`
    rerouted through `apply_paging_update_`.
  - `src/memory/mmu.h:975-1004` — declarations.
  - `src/memory/mmu.h:1043-1052` — new `port_1ffd_special_old_` member.
  - `src/memory/mmu.cpp:712-714, 763-764` — save/load state.

- **VHDL spec:**
  - `zxnext.vhd:3771` — `port_1ffd_special <= port_1ffd_reg(0)`.
  - `zxnext.vhd:3713-3742` — port_1ffd_reg / port_1ffd_special_old
    write process. Key: `port_1ffd_special_old` is captured from
    `port_1ffd_special` ONLY on a 1FFD write or NR $8E write (where
    `port_memory_change_dly='0'` at trigger time); on every other
    cycle the `else` branch at :3736-3738 clears it to '0'.
  - `zxnext.vhd:4623-4632` — when `port_1ffd_special='1'` AND a paging
    trigger fires, MMU0..MMU7 are all rewritten from the +3 special
    table indexed by `port_1ffd_reg(2:1)` (4 configs).
  - `zxnext.vhd:4653-4670` — when `port_1ffd_special_old='1'` (i.e.
    the previous-cycle value was 1) AND new `port_1ffd_special='0'`
    (we just exited special mode), MMU2/3 revert to {0x0A, 0x0B} and
    MMU4/5 to {0x04, 0x05}.

- **Pre-fix behaviour:**
  - `map_plus3_bank` only applied the special-paging table on the
    entry path. If the supervisor cleared bit 0 of port 1FFD (exiting
    special), slots 2-5 were left holding stale RAM bank pages — VHDL
    would have reverted them to bank 5 / bank 2 defaults.
  - `write_nr_8e` never honoured `port_1ffd_special` at all. Setting
    the special bit via NR $8E (bit 2) left the legacy paging in
    place; clearing it from a previously-set state never triggered
    the slots-2-5 revert.
  - `map_128k_bank`, `write_port_dffd`, `write_port_eff7`,
    `write_nr_8f` all unconditionally called `apply_legacy_paging_()`
    even when `port_1ffd_special=1`, clobbering the in-effect
    special-paging table.

- **Fix applied:**
  - Single arbiter `apply_paging_update_()` consults the current
    `port_1ffd_(0)` (= `port_1ffd_special`) and the cached
    `port_1ffd_special_old_` to choose between three branches —
    matching VHDL :4623-4684 exactly:
    1. `special=1` → write the +3 special table.
    2. `special=0 AND _old=1` → revert slots 2-5, then legacy paging.
    3. otherwise → legacy paging.
  - At end, `port_1ffd_special_old_ = special` so the next trigger
    detects transitions correctly.
  - `write_nr_8e` keeps a custom path so the bit-3=0 MMU6/7 suppression
    (VHDL :3814 + :4677) still applies; it routes through
    `apply_paging_update_()` only when `(special || exit_special ||
    bit3=1)`.
  - `port_1ffd_special_old_` is added to save/load state for
    determinism across save-states taken inside special paging.

- **Subtle VHDL-vs-jnext difference (verified non-observable):** VHDL
  decays `port_1ffd_special_old` to '0' on every cycle without a 1FFD
  or NR $8E write (else branch). jnext's `_old` persists at the
  last-set value. The MMU update only consults `_old` on the cycle of
  a paging trigger, and at that cycle VHDL's value comes from the
  same-cycle assignment by the trigger if it's a 1FFD/NR $8E write,
  or is '0' otherwise. jnext computes the same effective value because
  `apply_paging_update_` always sets `_old <- new special` after
  applying. Cross-checked by trace simulation of all paging-trigger
  permutations (1FFD-set / 1FFD-clear / NR $8E set / NR $8E clear /
  intervening 7FFD or DFFD or EFF7 or NR $8F writes) — outcome state
  identical. See the commit message for the worked examples.

- **Test status:** mmu_test 202/180/0/22, fuse_z80 1356/1356,
  full ctest 36/36 — all PASS.

- **G46(b) relevance:** The investigation traces (EOD-21 through
  EOD-24) consistently show the supervisor running in **+3 mode**
  (NR $03 = $03/$B3, machine_type committed to ZX_PLUS3) but **NOT**
  toggling +3 special paging mode (port_1ffd bit 0 stays 0
  throughout the boot). Hence this fix is not the immediate G46(b)
  trigger. It is, however, a latent correctness issue that would
  bite any tbblue.fw or NextZXOS code path that DOES use special
  paging.

### Non-finding 1 — sram_rom (NR $8E) atomicity

- **Location audited:** `src/memory/mmu.cpp:542-610` — `write_nr_8e`.
- **VHDL spec:** `zxnext.vhd:3662-3734` — NR $8E writes update
  `port_7ffd_reg`, `port_dffd_reg`, `port_1ffd_reg` in three concurrent
  `elsif nr_8e_we='1'` clauses, then `port_memory_change_dly` triggers
  the MMU rebuild.
- **Verification:** `write_nr_8e` updates all three port latches (7FFD,
  DFFD, 1FFD) **before** invoking the paging arbiter. The bit 3 = 0
  suppression of MMU6/7 (VHDL :3814 + :4677) is honoured. Bit 2 = 0
  forces 7FFD(4) <- bit 0 (per VHDL :3672-3674) — implemented at
  `mmu.cpp:554-558`. The `current_sram_rom()` fast-path accessor
  (`mmu.h:740-758`) re-derives the ROM bank from the LIVE port_7ffd /
  port_1ffd / NR $8C state on every CPU read, so atomicity is
  preserved. **No fix needed.**

### Non-finding 2 — port_7ffd / port_1ffd / port_dffd lock gating

- **Locations audited:** `src/memory/mmu.cpp:464-500` (map_128k_bank,
  map_plus3_bank, write_port_dffd), `src/memory/mmu.h:521-527`
  (`effective_paging_locked`).
- **VHDL spec:** `zxnext.vhd:3650, 3691, 3718` — all three port
  writes gated by `port_7ffd_locked='0'`. Line :3769 derives the lock
  from `port_7ffd_reg(5)`, with overrides for Pentagon-1024 mode
  (`nr_8f_mapping_mode_pentagon_1024_en`) and Profi mode
  (`nr_8f_mapping_mode_profi`).
- **Verification:** `effective_paging_locked()` correctly composes the
  lock with the Pentagon-1024 override (Profi is forced 0 per VHDL
  :3797). All three paging-port entry points (`map_128k_bank`,
  `map_plus3_bank`, `write_port_dffd`) early-return when locked.
  `write_port_eff7` and `write_nr_8e` correctly bypass the lock per
  VHDL :3780 and :3662/:3696/:3726 respectively. `unlock_paging()` is
  called from the NR $08 bit 7 path (paging unlock). **No fix needed.**

### Non-finding 3 — alt-ROM (NR $8C)

- **Locations audited:** `src/memory/mmu.h:280-291` (read path),
  `src/memory/mmu.h:386-391` (write path), `src/memory/mmu.h:924-949`
  (`altrom_sram_page_`).
- **VHDL spec:** `zxnext.vhd:2261-2266` — alt-ROM register layout.
  `zxnext.vhd:3056, 3078, 3116-3123` — alt-ROM SRAM routing. The
  read path triggers when `nr_8c_altrom_en=1 AND sram_pre_rdonly=1`
  (= altrom_rw=0); the write path triggers when
  `nr_8c_altrom_en=1 AND sram_pre_rdonly=0` (= altrom_rw=1). VHDL
  :3050 forces `sram_pre_override(0)='0'` in `nr_03_config_mode`,
  disabling alt-ROM in config mode.
  `zxnext.vhd:2981-3008` — `sram_alt_128_n` selector per machine type.
- **Verification:** `mmu.h::read` correctly gates altrom on
  `!config_mode_ && addr < 0x4000 && read_only_[slot]` (the
  ROM-area condition matches VHDL :3029 cpu_a(15:14)="00"), and
  re-derives `alt_128_n` per machine type matching VHDL :2986/:2988-2995/
  :2998-3005 lines. The SRAM page maps to {0x0C, 0x0D, 0x0E, 0x0F}
  (alt ROM0 128K + alt ROM1 48K) per VHDL :2924-2925. **No fix
  needed.**

### Non-finding 4 — NR $03 propagation (commits `144af1f` / `d376791`)

- **Location audited:** `src/core/emulator.cpp:1674-1781` — NR $03
  write/read handlers.
- **VHDL spec:** `zxnext.vhd:5124-5151` — NR $03 FSM (machine timing
  + machine type + config_mode). `zxnext.vhd:2981-3008` — derives
  `machine_type_48` / `machine_type_p3` signals from
  `nr_03_machine_type` and feeds them into the sram_rom selector.
- **Verification:** Handler propagates new machine type to **both**
  NextReg state AND `Mmu::set_machine_type()` (line 1752) **inside the
  config_mode=1 commit gate** matching VHDL :5137. Pentagon (typ_sel=4)
  maps to ZXN_ISSUE2 because VHDL :5751 maps it to machine_type_128
  (1-bit sram_rom branch). The im2 controller is also told via
  `set_machine_timing_48_or_p3()` for INT pulse width. **No fix
  needed.**

### Non-finding 5 — Soft reset (NR $02 bit 0)

- **Location audited:** `src/core/emulator.cpp:4891-4945` — `soft_reset()`.
- **VHDL spec:** `zxnext.vhd:1730` — top-level `reset` is
  `reset_hard OR reset_soft`. `zxnext.vhd:5052-5057` — NR $82-$84
  reload to 0xFF on soft reset only when `nr_85_reset_type='1'`.
  `zxnext.vhd:1101, 5109-5111, 5122` — `bootrom_en` gated by
  `nr_03_config_mode='1'` (which has no reset branch and persists
  across reset).
- **Verification:** `Mmu::reset()` already treats both hard and soft
  the same per VHDL :1730 (corrected by `144af1f`). `soft_reset()`
  preserves NR $82-$84 around `init()` per VHDL :5052-5057, and
  preserves the pre-reset `boot_rom_en` per VHDL :1101 logic. The
  bank-3 trampoline at $3BE8 (NR $02 soft-reset re-init point) is
  reached because `port_7ffd`/`port_1ffd` reset to 0 — which makes
  PC=$0000 land in bank 0 ROM (DI; JP $00EF). **No fix needed**
  (already correct on this worktree).

### Non-finding 6 — Boot ROM init for 48K / 128K / +3 / Next

- **Location audited:** `src/memory/mmu.cpp:21-50` (`set_boot_rom`),
  `src/memory/mmu.h:200-206` (read fast path).
- **VHDL spec:** `zxnext.vhd:1856` — boot ROM gates on
  `cpu_a(15:14)="00"` (16K window). `zxnext.vhd:3199-3204` — boot ROM
  is hardwired to `cpu_a(12:0)` (8K span); upper 8K mirrors lower 8K.
- **Verification:** `set_boot_rom` zero-pads / truncates input to
  exactly 8K and stores. The read fast path correctly gates on
  `addr < 0x4000` (16K window) and reads with `addr & 0x1FFF` (8K
  mirror). **No fix needed.**

### Non-finding 7 — NR $82-$84 / NR $85 port-enable gates

- **Locations audited:** `src/core/emulator.cpp:2284-2331` (port 0x7FFD),
  :2393-2428 (port 0x1FFD), :2582-2587 (port 0xDFFD),
  :2605-2610 (port 0xEFF7).
- **VHDL spec:** `zxnext.vhd:2393, 2399, 2400, 2415, 2441` — port
  enables come from `internal_port_enable` which is the concatenation
  `nr_85 & nr_84 & nr_83 & nr_82`. Bit 1 of NR $82 → port_7ffd;
  bit 3 of NR $82 → port_1ffd; bit 2 of NR $82 → port_dffd; bit 26
  of internal_port_enable (= NR $85 bit 2) → port_eff7.
- **Verification:** Each port handler checks its respective NR-bit
  before forwarding. The G143 fix (NR $85 bit 2 for EFF7) is
  correctly applied. **No fix needed.**

### Non-finding 8 — Contention semantics

- **Location audited:** `src/memory/mmu.h:312-316, 408-410` —
  `p3_floating_bus_dat_` capture on contended r/w.
- **VHDL spec:** `zxnext.vhd:4498-4509` — captures cpu_di on every
  contended memory read; cpu_do on contended write.
- **Verification:** `slot_contended_[]` is mirrored from
  `ContentionModel::set_contended_slot()` calls at the port-7FFD and
  port-1FFD handlers (per VHDL :4489-4493 contention rules: 128K odd
  banks, +3 bank≥4, +3 special-mode 4-config table). Read and write
  fast paths both update `p3_floating_bus_dat_`. **No fix needed.**

## Cross-check vs G46(b)

The G46(b) "supervisor stack divergence" bug (3 missing PUSHes / 3
extra POPs vs CSpect between RST $08 hits #2 and #3) is an
**execution-path** divergence, not a paging divergence at the moment
of analysis. Per EOD-23/EOD-24 traces:

- The **slide-trigger** is NEXTREG $8E,$03 at RAM $5B48 (a supervisor
  bank-flip wrapper). Its operation per VHDL :3662-3734 is the
  atomic update of port_7ffd(4) AND port_1ffd(2) → sram_rom=3 in one
  step. **jnext models this correctly** — the `write_nr_8e` handler
  is VHDL-faithful and the resulting bank flip is observed in the
  SRAM_ROM_TRACE probe.
- The **divergence point** is upstream — supervisor user code
  reaches $5B48 in jnext's per-cycle but never in CSpect's. The
  cause is in supervisor-stack semantics (specifically the PUSH/POP
  delta between RST $08 hits #2 and #3), not in MMU spec
  conformance.
- However, several **latent** memory-subsystem bugs that COULD have
  triggered or compounded the divergence have been verified absent:
  - NR $5x,$FF wrong-bank exposure: addressed by `f832f38` (and prior
    `8242098`) — the supervisor's transient `NR $51,$10 + NR $51,$FF`
    pattern at $15A0/$15B0 now correctly returns slot 1 to bank 1 hi
    (page 03) per current sram_rom rather than hardcoding bank 0 hi.
  - +3 special-paging misbehavior: addressed by `45d8b30` — though
    NextZXOS firmware does not exercise +3 special paging at the
    observed boot phase, this fix removes a latent correctness gap.
  - sram_rom non-atomic update: verified VHDL-faithful in
    `write_nr_8e`.
  - Alt-ROM mistiming: verified VHDL-faithful via the `altrom_rw=0`
    read-only / `altrom_rw=1` write-over branches.
  - NR $03 → MMU machine_type propagation: verified faithful (the
    `144af1f` / `d376791` chain is already in this branch's history).
  - Lock blocking legitimate writes: verified — NR $8E and EFF7
    bypass the lock as VHDL specifies; locked writes are silently
    dropped per VHDL.

**Bottom line:** the memory subsystem is now VHDL-faithful for every
NR / port path the NextZXOS supervisor exercises during the observed
boot cascade. G46(b) is unlikely to be solved by additional
memory-subsystem fixes; the next investigation step (per EOD-24) is to
trace PUSH/POP between RST $08 hits #2 and #3 to pinpoint the
supervisor-stack bug.

## Open questions

1. The `port_1ffd_special_old_` decay-vs-persist subtlety in
   `apply_paging_update_` (vs VHDL :3736-3738) is functionally
   equivalent for all permutations I traced, but a strictly
   bit-faithful model would clear `_old` on every non-1FFD/non-NR $8E
   paging trigger. If we ever find a path where the discrepancy
   matters, the fix would be to scope `_old` updates to 1FFD/NR $8E
   triggers only, and clear it elsewhere. Out of scope for the
   observed boot path.

2. NR $50-$57 on slots 2-5 / slots 6-7 with a non-$FF, non-physical-
   page value — VHDL just stores the value, and our `set_page` does
   the same with `to_sram_page` shift in Next mode. Already covered.

3. The rare combination of `port_eff7_reg_3=1` (RAM-at-0x0000) AND
   `nr_8f_mapping_mode_profi=1` AND `port_dffd_reg(4)=1` (VHDL
   :4636) — the legacy ROM paging branch then forces MMU0=0x00,
   MMU1=0x01 even when in standard config. Not exercised in NextZXOS
   boot (Profi mode is forced off in jnext per VHDL :3797). The
   `engage_legacy_rom_paging_slot()` helper already handles
   `port_eff7_reg_3` but not the Profi+DFFD bit-4 condition. If Profi
   were ever enabled, that would need attention. Out of scope here.
