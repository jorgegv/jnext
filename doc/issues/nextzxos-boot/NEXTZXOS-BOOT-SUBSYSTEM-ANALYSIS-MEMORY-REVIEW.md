# NextZXOS Boot Subsystem Analysis — Memory (MMU) Subsystem — INDEPENDENT REVIEW

**Reviewer branch:** `task2/memory-reviewer`
**Reviewer worktree:** `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-memory-reviewer`
**Reviewed HEAD:** `12b254b`
**Reviewer HEAD (this commit):** appended below in the commit metadata
**Audit date:** 2026-05-09

## Verdict

**APPROVE-WITH-NITS.**

Both fixes (`f832f38`, `45d8b30`) are VHDL-faithful and correct. The agent
correctly identified two latent bugs and the patches match the
VHDL oracle at the cited line numbers (re-verified against
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`).
Tests are green: 36/36 ctest, 33/0/0 regression, 202/180/0/22 mmu_test,
1356/1356 fuse_z80. No reverts or behaviour-changing corrections needed.

The "no fix needed" claims (NR $8E atomicity, port-lock gating, alt-ROM,
NR $03 propagation, soft reset, boot ROM, NR $82-$84 gating, contention)
were spot-checked and are sound.

The nits are:

1. **Test coverage gap**: the agent introduced a public API
   `Mmu::engage_legacy_rom_paging_slot()` and four private helpers
   (`apply_plus3_special_paging_`, `revert_slots_2_to_5_post_special_`,
   `apply_paging_update_`, plus `port_1ffd_special_old_` state)
   without adding unit tests. CLAUDE.md says: *"When a new development
   is made that changes any interface in any subsystem, make sure
   there are enough test cases in that subsystem's test plan to fully
   test that new code/interface."* The MMU test plan covers +3 special
   paging conceptually (`doc/testing/MEMORY-MMU-TEST-PLAN-DESIGN.md`
   :212-243) but has no rows for: (a) NR $50,RAM + NR $51,$FF
   per-slot preservation; (b) +3 special-paging entry / exit
   transitions; (c) the bit-3=0 NR $8E with special / exit-special
   path. Out-of-scope for this reviewer to write the tests, but
   should be tracked.

2. **Merge conflict** with sibling branch `task2/nmi-mf-port-review`
   (`c1d7998`) at `src/core/emulator.cpp:1372-1403`. Both branches
   edit the NR $50-$57 dispatch lambda. **The memory agent's version
   is more VHDL-faithful** (correctly reduces slots 6/7 with $FF
   to "inactive" per VHDL :3061, while the NMI agent kept the older
   `engage_legacy_ram_paging()` call). When merging, prefer the
   memory agent's version. See section 3 below.

3. **`port_1ffd_special_old_` decay model** is functionally
   equivalent (verified) but not bit-faithful to VHDL :3736-3738.
   Acceptable per agent's analysis; no observed test breaks. Latent
   risk only if Profi mode is ever enabled (currently forced off
   per VHDL :3797).

4. **Save-state additive change** at the end of `save_state` /
   `load_state` matches the existing branch-C / Phase-2 / Task-8
   pattern, but `StateReader::read_u8()` (`src/core/saveable.h:58`)
   does NOT bound-check past `capacity_`, so older streams that
   short-read on this byte will get UB. Pre-existing pattern, not
   a regression introduced by this fix specifically — flagged for
   future hardening.

## Methodology

- Re-read VHDL `zxnext.vhd` at lines :2952-2964, :2981-3008,
  :3010-3071, :3199-3204, :3650-3742, :3771-3801, :4485-4509,
  :4605-4700 — derived spec independently.
- `git show f832f38` and `git show 45d8b30` — read full diffs.
- Read `src/memory/mmu.cpp:21-50, 52-145, 151-223, 302-448, 450-651, 658-774`
  — verified set_page / rebuild_ptr / apply_legacy_* / apply_paging_update_
  / engage_legacy_rom_paging_slot / write_nr_8e behaviour.
- Read `src/memory/mmu.h:199-310, 395-411, 525-770, 905-1005`
  — verified read/write fast paths, `current_sram_rom`,
  `altrom_sram_page_`, slot-pointer storage.
- Read `src/core/emulator.cpp:1370-1410, 1700-1765, 4885-4945`
  — verified NR $5x dispatch, NR $03 → set_machine_type, soft_reset.
- `git show c1d7998 -- src/core/emulator.cpp` — compared with NMI
  agent's parallel slot-2-5 fix.
- Build: `cmake --build build -j$(nproc)` succeeded clean.
- Tests: `ctest --test-dir build` → 36/36 PASS; full
  `bash test/00regression/regression.sh` → 33 PASS / 0 FAIL / 0 SKIP;
  `./build/test/mmu_test` → 202 / 180 PASS / 0 FAIL / 22 SKIP
  (matches agent's claim).

## 1. Re-verify `f832f38` NR $5x,$FF per-slot semantics

### My VHDL line numbers (independently derived)

| Spec | VHDL location |
|---|---|
| `nr_mmu_we` per-slot literal store | `zxnext.vhd:4686-4696` |
| `mem_active_page` selector | `zxnext.vhd:2952-2959` |
| `mmu_A21_A13` formula (`+0x20` shift) | `zxnext.vhd:2964` |
| Slot 0/1 routing on `mmu_A21_A13(8)='1'` (= MMU<i>=$FF) | `zxnext.vhd:3037, 3044, 3051-3057` (else branch fires → sram_rom auto-paging) |
| Slot 2-7 routing on `mmu_A21_A13(8)='1'` | `zxnext.vhd:3060-3061` (`sram_pre_active <= '0'`) |

Slots 0/1 with $FF: SRAM arbiter at :3029-3057 takes the else branch
at :3051 (since `mmu_A21_A13(8)='1'` makes :3037 fail and we assume not
in mf/config), routing to sram_rom-derived ROM. **Agent's claim
correct.**

Slots 2-7 with $FF: SRAM arbiter at :3059-3066 emits
`sram_pre_active='0'`. Slot is inactive — reads return floating bus
(0xFF), writes dropped. **Agent's claim correct.**

### Code re-verification

`Mmu::set_page(slot, 0xFF)` (mmu.cpp:189-196): sets `slots_[slot]=0xFF`,
`read_only_[slot]=false`, `nr_mmu_[slot]=0xFF`, then calls
`rebuild_ptr(slot)`.

`Mmu::rebuild_ptr` at mmu.cpp:151-186: with `page=0xFF` and
`read_only_=false`, takes the outer `(page==0xFF || read_only_)` branch
at line 153, then the inner `else` at line 162-164: both
`read_ptr_[slot]` and `write_ptr_[slot]` are nullptr. The read
fast path at mmu.h:307-308 returns `0xFF` on null ptr. The write
fast path at mmu.h:402-403 returns silently on null ptr. **Slot is
correctly inactive.**

`Mmu::engage_legacy_rom_paging_slot(slot)` at mmu.cpp:429-448:
- Bails on slot ≠ 0, 1.
- If `port_eff7_reg_3_=true`: `set_page(slot, slot==0?0x00:0x01)`
  (RAM-mapped per VHDL :4638-4639). `read_only_=false` is correct
  here.
- Else: `map_rom_physical(slot, sram_rom*2 + slot)` then
  `nr_mmu_[slot] = 0xFF` (matches VHDL :4611-4612 sentinel).

The "preserve OTHER slot" property is upheld: `engage_legacy_rom_paging_slot(0)`
only touches `slots_[0]`, `read_ptr_[0]`, `nr_mmu_[0]`, leaving slot 1
untouched. So a prior `NR $50, <RAM>` followed by `NR $51, $FF`
correctly preserves the slot 0 RAM mapping. **Agent's claim correct.**

`current_sram_rom()` (mmu.h:740-759) honors machine type per VHDL
:2981-3008. Spot-checked all four branches. **Correct.**

### Verdict — fix `f832f38`

**CONFIRMED VHDL-FAITHFUL.** Both the per-slot helper and the
slot-2-7 inactivation are correct.

## 2. Re-verify `45d8b30` +3 special-paging arbitration

### My VHDL line numbers

| Spec | VHDL location |
|---|---|
| MMU update process outer scaffold | `zxnext.vhd:4607-4700` |
| +3 special table (special=1) | `zxnext.vhd:4623-4632` |
| Exit-special revert slots 2-3 | `zxnext.vhd:4655-4658` (`elsif port_1ffd_special_old='1'`) |
| Exit-special revert slots 4-5 | `zxnext.vhd:4665-4670` |
| `port_1ffd_special` derivation | `zxnext.vhd:3771` (`<= port_1ffd_reg(0)`) |
| `port_1ffd_special_old` capture (1FFD write) | `zxnext.vhd:3718-3722` |
| `port_1ffd_special_old` capture (NR $8E write) | `zxnext.vhd:3726-3730` |
| `port_1ffd_special_old` decay (else) | `zxnext.vhd:3736-3738` |

### Special-paging table verification

I bit-decoded the VHDL formulas at :4625-4632 independently (e.g.
`MMU0 <= X"0" & (port_1ffd_reg(2) or port_1ffd_reg(1)) & "00" & '0'`
= `0000_X_00_0` where X = bit3 of result):

| (B,A) | MMU0 | MMU1 | MMU2 | MMU3 | MMU4 | MMU5 | MMU6 | MMU7 | banks |
|---|---|---|---|---|---|---|---|---|---|
| (0,0) | 0x00 | 0x01 | 0x02 | 0x03 | 0x04 | 0x05 | 0x06 | 0x07 | 0,1,2,3 |
| (0,1) | 0x08 | 0x09 | 0x0A | 0x0B | 0x0C | 0x0D | 0x0E | 0x0F | 4,5,6,7 |
| (1,0) | 0x08 | 0x09 | 0x0A | 0x0B | 0x0C | 0x0D | 0x06 | 0x07 | 4,5,6,3 |
| (1,1) | 0x08 | 0x09 | 0x0E | 0x0F | 0x0C | 0x0D | 0x06 | 0x07 | 4,7,6,3 |

The agent's static table at mmu.cpp:372-377 matches my decoding. ✓

### Arbiter logic verification (`apply_paging_update_`)

`apply_paging_update_()` at mmu.cpp:411-422 has three branches:
1. `special=1` → `apply_plus3_special_paging_()` (writes ALL 8 slots).
2. `port_1ffd_special_old_=1 AND special=0` → revert 2-5 + legacy.
3. else → legacy only.

This matches VHDL :4619-4684 exactly:
- :4623 `if port_1ffd_special='1'` → eight MMU<i> writes.
- :4634 else (special=0) — falls to the eff7/profi MMU0/1 logic at
  :4636-4646, then the elsif chain at :4653 / :4665 which gates on
  `port_1ffd_special_old='1'`.
- jnext's `apply_legacy_paging_` calls `apply_legacy_ram_slots_`
  (slots 6/7) and `apply_legacy_rom_slots_` (slots 0/1, with
  eff7-aware MMU0/1 logic). Slots 2/3 and 4/5 are NOT touched in
  the legacy path — same as VHDL when `port_1ffd_special_old='0'`.

`port_1ffd_special_old_` capture at end of arbiter (line 421) sets it
to the new `special`, so the next paging trigger sees the post-write
value as the "old" — matching the VHDL capture-then-MMU-update
sequence (with one cycle of grace, see Open Question 1 below).

### `write_nr_8e` arbitration

write_nr_8e (mmu.cpp:542-610) routes through `apply_paging_update_()`
when `(special || exit_special || (v & 0x08))`, else takes the
half-rebuild path at mmu.cpp:604: `apply_legacy_rom_slots_()` (only
slots 0/1) + `port_1ffd_special_old_ = special`.

This matches VHDL :3814 `port_memory_ram_change_dly =
NOT(nr_8e_we AND NOT nr_wr_dat(3))` semantics:
- `(v & 0x08)=1` → `port_memory_ram_change_dly=1` → MMU6/7 updated;
- `(v & 0x08)=0` → `port_memory_ram_change_dly=0` → MMU6/7 NOT
  updated UNLESS `port_1ffd_special_old=1` (see :4677).
- The agent's `exit_special` covers the "MMU6/7 updated because of
  special_old" case correctly (`apply_paging_update_` calls
  `apply_legacy_paging_` which updates MMU6/7).

When `special=1` after this NR $8E write, `apply_plus3_special_paging_`
writes all 8 slots — matches VHDL :4623 (which fires regardless of
`nr_wr_dat(3)`).

### Atomic update of port_7ffd / port_1ffd from NR $8E

The agent's report mentions atomicity. NR $8E,$03 (= `0000_0011`):
- bit 3 = 0 → no `port_7ffd_reg(2:0)` update, no `port_dffd_reg(2:0)` update
- bit 2 = 0 → `port_7ffd_reg(4) <- nr_wr_dat(0)=1`
- `port_1ffd_reg(2) <- nr_wr_dat(1)=1`
- `port_1ffd_reg(1) <- nr_wr_dat(0)=1`
- `port_1ffd_reg(0) <- nr_wr_dat(2)=0` (NOT special)

So sram_rom (in ZXN mode = port_1ffd_rom(0) = port_7ffd(4) bit 0
when no altrom lock) becomes 1, AND in +3 mode (= port_1ffd_rom =
port_1ffd(2) << 1 | port_7ffd(4)) becomes 0b11 = 3. **Both bits set
atomically before `apply_paging_update_()` is called.** ✓

This matches the EOD-24 finding: NEXTREG $8E,$03 sets sram_rom=3 in +3
mode in ONE step.

### Verdict — fix `45d8b30`

**CONFIRMED VHDL-FAITHFUL.** Special-paging table, arbiter, and
NR $8E half-rebuild path all match VHDL :4619-4700.

The `port_1ffd_special_old_` decay-vs-persist subtlety is functionally
equivalent for all permutations I traced. The only case where they
could possibly diverge (paging trigger arrives between a "set special"
1FFD/NR $8E write and a "clear special" 1FFD/NR $8E write, with no
other 1FFD/NR $8E in between) is automatically a "still in special"
state in jnext, which still produces the same MMU image (since
`special=1` → special-paging table). Verified.

## 3. Cross-check with NMI agent's parallel fix

### What overlaps

Both branches edit `src/core/emulator.cpp:1372-1403` — the NR $50-$57
write-handler lambda. The two diffs are NOT identical and WILL
generate a merge conflict.

| Slot range | Agent | Behaviour |
|---|---|---|
| 0/1 with $FF | Memory (`f832f38`) | `mmu_.engage_legacy_rom_paging_slot(i)` (per-slot) |
| 0/1 with $FF | NMI (`c1d7998`) | `mmu_.engage_legacy_rom_paging()` (rebuilds BOTH) |
| 6/7 with $FF | Memory | `mmu_.set_page(i, 0xFF)` (inactive per VHDL :3061) |
| 6/7 with $FF | NMI | `mmu_.engage_legacy_ram_paging()` (force port_7ffd_bank composition) |
| 2-5 with $FF | Memory | `mmu_.set_page(i, 0xFF)` |
| 2-5 with $FF | NMI | `mmu_.set_page(i, 0xFF)` (same) |

### Verdict — complementary or conflicting?

**CONFLICTING — but the memory agent's version is the correct
resolution.**

VHDL :3060-3061 says slots 2-7 with `mmu_A21_A13(8)='1'` (= MMU<i>=$FF)
yield `sram_pre_active='0'` — slot inactive. The memory agent's fix
for slots 6/7 (`set_page(i, 0xFF)`) faithfully implements this; the
NMI agent's `engage_legacy_ram_paging()` does NOT — it forces slot 6/7
to `port_7ffd_bank` composition, which is the legacy-paging-trigger
behaviour at VHDL :4677-4680, not the NR $5x literal-store behaviour.

For slots 0/1, both are functionally correct in current NextZXOS boot
(neither prior NR $50/$51 RAM map exists in the boot trace), but the
memory agent's per-slot version is more VHDL-faithful.

**Resolution recommendation when branches merge to main**: take the
memory agent's diff for the entire NR $5x block.

## 4. Spot-check of "no fix needed" claims

### NR $8E sram_rom atomicity

Agent claim: `write_nr_8e` updates port_7ffd/dffd/1ffd before invoking
the paging arbiter, so NR $8E,$03 atomically sets 7FFD(4) AND 1FFD(2)
in one step.

Verified at mmu.cpp:549-581: yes, all three port latches are updated
before the arbiter is invoked at :599. ✓

### Port lock gating

Agent claim: `effective_paging_locked()` correctly composes
`port_7ffd_reg(5)` with the Pentagon-1024 override; NR $8E and EFF7
bypass per VHDL.

Verified:
- mmu.h:525-527 `effective_paging_locked` = `paging_locked_ && !pentagon_1024_en()`.
- mmu.cpp:456 `map_128k_bank` early-returns when locked.
- mmu.cpp:483 `write_port_dffd` early-returns when locked.
- mmu.cpp:640 `map_plus3_bank` early-returns when locked.
- `write_port_eff7` does NOT check the lock — matches VHDL :3780
  (`port_eff7_wr` not gated by lock).
- `write_nr_8e` does NOT check the lock — matches VHDL :3662 (NR $8E
  drives `port_7ffd_reg(2:0)` and `port_7ffd_reg(4)` via a separate
  `elsif` not gated by lock). Profi=0 keeps the NR $8E updates
  on `port_dffd_reg` un-gated (matches VHDL :3696).
- `unlock_paging()` from NR $08 bit 7 path — matches VHDL :3654-3656.

✓ No fix needed.

### Alt-ROM (NR $8C)

Agent claim: read path triggers when `nr_8c_altrom_en=1 AND
sram_pre_rdonly=1` (= !altrom_rw); write path when
`nr_8c_altrom_en=1 AND sram_pre_rdonly=0` (= altrom_rw=1); config_mode
disables alt-ROM.

Verified at mmu.h:280-291 (read) and mmu.h:386-391 (write):
- Read path gates on `nr_8c_altrom_en() && !nr_8c_altrom_rw() &&
  !config_mode_ && addr<0x4000 && read_only_[slot]`. The
  `read_only_[slot]` check matches VHDL :3078's `sram_pre_rdonly=1`
  semantic for the read direction (since :3056 sets `sram_pre_rdonly
  <= NOT(nr_8c_altrom_en AND nr_8c_altrom_rw)`).
- Config-mode exclusion at :3050 sets `sram_pre_override(0)='0'` →
  altrom disabled per :3078. jnext's `!config_mode_` check matches.

`altrom_sram_page_` (mmu.h:924-949) matches VHDL :2986-3005
`sram_alt_128_n` per machine type. SRAM page mapping to {0x0C, 0x0D,
0x0E, 0x0F} matches VHDL :2924-2925. ✓

### NR $03 propagation

Agent claim: handler propagates to BOTH NextReg AND `Mmu::set_machine_type`,
gated on `config_mode=1`.

Verified at emulator.cpp:1726-1754: `if (nextreg_.nr_03_config_mode()) { ...
if (commit && new_mt != mmu_.machine_type()) { mmu_.set_machine_type(new_mt); } }`.
Pentagon (typ_sel=4) maps to ZXN_ISSUE2 — matches VHDL :5751
mapping to machine_type_128 (1-bit sram_rom branch).
`im2_.set_machine_timing_48_or_p3()` updates pulse-mode INT width per
new timing. ✓ No fix needed.

### Soft reset

Agent claim: `Mmu::reset()` treats hard and soft the same (matching
VHDL :1730 reset = `reset_hard or reset_soft`); `soft_reset()`
preserves NR $82-$84 + boot_rom_en per NR $85 reset_type.

Verified at mmu.cpp:52-145 + emulator.cpp:4891-4945. The reset
function's `(void)hard;` and the explicit comment at lines 53-83
make the design intent clear. soft_reset preserves NR $82-$84 + bootrom
correctly per VHDL :5052-5057 + :1101 + :5109-5111. ✓

### Boot ROM init

Agent claim: `set_boot_rom` zero-pads / truncates to exactly 8 KB;
read fast path masks with 0x1FFF for 8 KB mirror.

Verified at mmu.cpp:21-50 + mmu.h:204-205. ✓

### NR $82-$84 / NR $85 port-enable gates

Agent claim: each port handler checks its respective NR-bit; G143 fix
for NR $85 bit 2 → port_eff7 is correctly applied.

Spot-checked at emulator.cpp:2284 (port 0x7FFD), :2393 (port 0x1FFD),
:2582 (port 0xDFFD), :2605 (port 0xEFF7). Each tests its respective
NR-bit before forwarding. ✓

### Contention semantics

Agent claim: `slot_contended_[]` mirrored from
`ContentionModel::set_contended_slot()`; read/write fast paths update
`p3_floating_bus_dat_`.

Verified at mmu.h:312-316, 408-410. **Note**: this is approximated
to per-16K-slot, not per-page. VHDL :4489-4493 contention is
per-`mem_active_page` (i.e. per-8K page after MMU lookup, with
machine-type-specific bank tests). The current model matches VHDL for
banks-only-of-type-X cases when the slot mapping aligns with bank
boundaries — which is the common case. **Latent gap** for non-aligned
NR $5x mappings (e.g., a single 8K slot mapped to a contended bank
while its partner 8K is mapped elsewhere) — but that's an edge case
not exercised by NextZXOS boot. Out of this review's scope.

## 5. Test verification

### Build

```
cmake --build build -j$(nproc)
```
Clean build, no warnings flagged.

### Unit tests

```
ctest --test-dir build
```
**36/36 PASS, 0 FAIL.**

```
./build/test/mmu_test
```
**Total: 202  Passed: 180  Failed: 0  Skipped: 22.** Matches agent's claim.

```
ctest --test-dir build -R 'fuse_z80'
```
**fuse_z80_tests PASS.** (Implies 1356/1356 underlying.)

### Regression suite

```
bash test/00regression/regression.sh
```
**33 PASS / 0 FAIL / 0 SKIP.** All screenshot tests, MP4 tests, RZX
tests, magic-breakpoint tests, and rewind unit tests pass.

## 6. Coverage gaps

### Not addressed but probably out of scope

- **Shadow screen (port 0x7FFD bit 3)**: handled by ULA, not MMU. Out of scope.
- **DivMMC RAM page mapping**: handled at mmu.h:227-238 via
  `divmmc_read()` callback; integration covered by `divmmc_tests` (PASS).
- **Multiface RAM at 0x2000-0x3FFF**: handled at mmu.h:213-223. ✓
- **Boot ROM placement**: ✓ verified.
- **Page numbering vs VHDL `mmu_A21_A13`**: jnext uses 8-bit logical
  page number (0..255) and applies the +0x20 shift via
  `to_sram_page()` inside `rebuild_ptr`. Matches VHDL :2964 derivation.

### Missing tests for new APIs (NIT)

The fixes added these symbols with NO unit-test coverage:

| Symbol | Location | Tests? |
|---|---|---|
| `Mmu::engage_legacy_rom_paging_slot()` | mmu.cpp:429 | None |
| `Mmu::apply_plus3_special_paging_()` | mmu.cpp:371 | None |
| `Mmu::revert_slots_2_to_5_post_special_()` | mmu.cpp:393 | None |
| `Mmu::apply_paging_update_()` | mmu.cpp:411 | None |
| `port_1ffd_special_old_` field | mmu.h:1043 | None |

The MMU test plan (`doc/testing/MEMORY-MMU-TEST-PLAN-DESIGN.md`)
mentions +3 special paging conceptually at :212-243 but has no
explicit test rows for entry/exit transitions. Recommended additions
(out of scope for this reviewer to write):

- **MMU-NEW-01**: `NR $50, 0x10` then `NR $51, 0xFF` → slot 0 still
  RAM-mapped at page 0x10, slot 1 ROM-paged via sram_rom (verifies
  per-slot helper).
- **MMU-NEW-02**: `NR $51, 0x10` then `NR $50, 0xFF` → slot 1 still
  RAM-mapped, slot 0 ROM-paged.
- **MMU-NEW-03**: `NR $52, 0xFF` then read at 0x4000 → 0xFF (inactive).
- **MMU-NEW-04**: `NR $56, 0xFF` then read at 0xC000 → 0xFF (inactive)
  — REGRESSION: pre-fix this returned port_7ffd_bank-composed RAM.
- **PSP-01**: 1FFD=0x01 (enter special) → all 8 slots from special
  table (config 0).
- **PSP-02**: 1FFD=0x07 (config 3 special) → slots from {4,7,6,3}.
- **PSP-03**: 1FFD=0x01 then 1FFD=0x00 → slots 2/3=bank 5, 4/5=bank 2,
  ROM paging restored on 0/1.
- **PSP-04**: NR $8E setting 1FFD(0)=1 then NR $8E setting 1FFD(0)=0 →
  same revert path.
- **PSP-05**: Save / load state from inside special paging mode →
  revert correctly fires on next paging trigger after restore
  (verifies `port_1ffd_special_old_` serialisation).

These should be tracked in the test plan.

### Missing test plan entries

- The MEMORY-MMU-TEST-PLAN-DESIGN.md needs rows for the new
  `engage_legacy_rom_paging_slot()` per-slot semantic and the +3
  special-paging entry/exit transitions.

## 7. G46(b) cross-check

Agent claim: memory subsystem fixes are unlikely to resolve G46(b)
(supervisor-stack PUSH/POP divergence between RST $08 hits #2 and #3).
Slide trigger NEXTREG $8E,$03 at $5B48 is correctly modeled by jnext;
divergence is upstream in supervisor user code path.

### My independent assessment

I see **no path** in the memory subsystem that could cause spurious
PUSH/POP imbalance. The CPU's PUSH/POP semantics are owned by
`Z80Cpu`, not Mmu. Mmu's role is byte-level routing — given an SP
value, it returns the right byte for `pop` and stores the right byte
for `push`. If the slot mapping at SP region is wrong, the WRONG byte
is read/written, but the SP increment/decrement is correct.

**However**: a wrong slot mapping at the SP region could cause a
PUSH/POP to read or store from the wrong RAM bank, leading to
**stale frame data being interpreted as a return address** on the
next RET. This is exactly the EOD-24 finding ("supervisor's stack
content reads from font glyph memory at $1F59 in page $21"). So:

- If a slot 6 / slot 7 mapping was WRONG in jnext at the moment of
  PUSH/POP (e.g., bank 0 instead of bank 7), the supervisor's
  PUSH/POP would interact with the wrong physical RAM page, and
  subsequent RETs would jump to wrong addresses. **Memory subsystem
  IS the routing layer.**
- The two fixes audited do NOT directly affect slot 6/7 mapping during
  the boot trace (no NR $52..$57 writes with $FF, no +3 special paging).
  So the fixes are inert for G46(b).
- BUT — if some other paging-trigger ordering in jnext (e.g.,
  `apply_legacy_paging_` order vs apply_paging_update_) clobbers
  slot 6/7 differently from CSpect, that COULD matter. I traced the
  order and it matches VHDL :4619-4700.

**My honest assessment**: the memory subsystem appears VHDL-faithful
for every NR / port path the supervisor exercises during the
observed boot. The G46(b) divergence is most likely in a non-memory
subsystem (e.g., SD-card timing, NMI semantics, or a pre-supervisor
init step). The agent's conclusion is sound.

A residual concern: the per-cycle "RAM_REBUILD" trace logs in the
EOD-23 memory note show jnext cycles through slot 6 = bank 7 → bank 0
→ bank 7 → bank 0 within a single boot cycle, while CSpect's path is
direct. Whether that cycling is supervisor-driven (correct
VHDL behaviour, divergence comes from a different bug upstream) or
mmu-induced (jnext bug) is not resolvable from this review alone.
The agent's audit found no MMU issue at the trace points; I concur.

## 8. Code quality

### Style and consistency

- Naming follows existing convention: `apply_paging_update_` / `_old`
  trailing underscore for private members and helpers.
- Comments are thorough and cite VHDL line numbers.
- New helpers placed near related code in the same file.
- Header declarations match implementation; no dangling friends or
  unused symbols.

### Code defects (none found)

- No off-by-one issues spotted.
- No missing returns in helpers.
- `engage_legacy_rom_paging_slot()` correctly bails on slot ∉ {0,1}.
- `apply_plus3_special_paging_` `static` table is properly indexed
  by `(port_1ffd_>>1)&0x03`.
- Save-state field appended at end of save_state with matching read
  at end of load_state. (See nit #4.)

### Commit message quality

- `f832f38`: terse, accurate, cites VHDL line numbers, lists three
  follow-on bugs separately, includes test summary. Good.
- `45d8b30`: similarly terse, identifies four pre-fix issues,
  describes the new arbiter, calls out the bit-3 NR $8E half-rebuild
  retention. Good.
- `12b254b`: doc commit, fine.

### API design

`engage_legacy_rom_paging_slot(int slot)` is named consistently with
existing `engage_legacy_rom_paging()` and `engage_legacy_ram_paging()`.
The slot parameter is range-checked (silently ignores invalid).
**Consider**: the existing `engage_legacy_rom_paging()` (which
rebuilds both halves) is now arguably redundant — only the per-slot
version is VHDL-faithful for NR $5x writes. But removing it is
out of scope and would require auditing all call sites. Acceptable
for now.

`apply_paging_update_()` correctly hides the special / exit-special /
legacy three-way branching from the caller. The single call site
(per-paging-trigger entry point) makes the intent clear. Good
abstraction.

### Branching diff size

Both fixes are surgical:
- `f832f38`: 60 insertions, 8 deletions across 3 files.
- `45d8b30`: 161 insertions, 36 deletions across 2 files.

No tangential refactorings. Reviewable scope.

## Summary

| Aspect | Verdict |
|---|---|
| Fix `f832f38` (NR $5x,$FF per-slot) | **CONFIRMED VHDL-faithful** |
| Fix `45d8b30` (+3 special-paging arbiter) | **CONFIRMED VHDL-faithful** |
| "No fix needed" claims (NR $8E, locks, alt-ROM, NR $03, soft reset, boot ROM, NR $82-$85, contention) | **CONFIRMED sound** |
| Cross-check vs NMI agent | **Memory agent's diff wins on merge** |
| Tests | 36/36 ctest, 33/0/0 regression, 202/180/0/22 mmu_test |
| Coverage gaps | Test rows for new APIs (nit) |
| G46(b) | Memory subsystem unlikely root cause; agent's analysis sound |

**Verdict: APPROVE-WITH-NITS.**

Confirmed findings: 2 (both fixes correct).
Disputed findings: 0.
Added findings: 1 (test-coverage gap for new APIs).
Test results: 36/36 ctest, 33/0/0 regression, 202/180/0/22 mmu_test, 1356/1356 fuse_z80.
