# Pass-12 Memory subsystem audit — verify12-memory

**Branch**: `task2/verify12-memory` (off integration HEAD `df247c8`)
**Worktree**: `.claude/worktrees/task2-verify12-memory`
**Methodology**: blind audit (no prior reports read), VHDL-as-oracle,
no probes, semantic fixes only. Release-mode build.

## Summary

| Class | Count |
|-------|-------|
| (a) boot-critical / breaks software | 0 |
| (b) VHDL divergence, observable surface | 3 |
| (c) detectable surface, weakly cited | 0 |
| (d) architectural / out-of-scope | 0 |
| **Total** | **3** |

Tests: ctest 38/38 PASS, mmu_integration 11/11 PASS (3 new
discriminative rows added), FUSE Z80 1356/1356 PASS.

Commit: `ce1d3be` — `fix(task2-verify12-memory): pass-12 memory
subsystem fixes (3 findings)`.

## Findings

### V12-MEM-01 — NR 0x8C / set_machine_type clobbers `nr_mmu_[]`

* **Classification**: (b) — VHDL register read-back divergence
* **VHDL oracle**:
  - `zxnext.vhd:4607-4700` — MMU<i> register process. Writes ONLY on
    `reset='1'` (line 4610) / `port_memory_change_dly='1'` (line 4619)
    / `nr_mmu_we='1'` (line 4686).
  - `zxnext.vhd:3813` — `port_memory_change_dly` composition. NR 0x8C
    is NOT in the OR list; an NR 0x8C write does NOT pulse the rebuild.
  - `zxnext.vhd:4880-4881` — `nr_mmu_we` fires ONLY on NR 0x50..0x57.
  - `zxnext.vhd:6075-6082` — NR 0x50..0x57 read-back returns the live
    MMU<i> register byte verbatim.
* **Root cause**: jnext's `set_nr_8c()` (mmu.cpp) and `set_machine_type()`
  (mmu.h) called `engage_legacy_rom_paging_slot()` to refresh the read
  pointer for slots 0/1 currently in legacy-ROM mode (`read_only_=true`).
  The helper unconditionally set `nr_mmu_[slot] = 0xFF`. This was
  correct for the ONE legitimate caller (the NR $50/$51=$FF dispatcher,
  matching VHDL `nr_mmu_we` storing 0xFF verbatim), but wrong for the
  NR 0x8C and machine-type-change call sites — VHDL leaves MMU<i>
  untouched on those triggers (no `nr_mmu_we`, no
  `port_memory_change_dly`). A previously-stored verbatim NR 0x50/0x51
  value in 0xE0..0xFE (legal high-page mapping that resolves to legacy
  ROM via the SRAM arbiter at :3037-3057 but reads back the verbatim
  NR-write byte) — and the EFF7(3)=1-derived 0x00/0x01 verbatim
  mapping (:4636-4644) — was silently collapsed to 0xFF on the next
  NR 0x8C / NR 0x03 machine-type-change write.
* **Fix**: Add a `set_nr_sentinel` parameter to
  `Mmu::engage_legacy_rom_paging_slot(int slot, bool set_nr_sentinel = true)`.
  The NR $50/$51=$FF dispatcher passes `true` (existing behaviour).
  The NR 0x8C handler (`Mmu::set_nr_8c`) and set_machine_type refresh
  pass `false` so `nr_mmu_[slot]` is preserved.
* **Regression test**: `V12-MEM-01-A`, `V12-MEM-01-B` in
  `test/mmu/mmu_integration_test.cpp:test_nr_8c_preserves_nr_mmu`.
  Discriminative pair: write NR 0x50=0xE5; read back 0xE5; write NR 0x8C
  with lock bits set; read NR 0x50 — must STILL return 0xE5 (not 0xFF).
* **Commit**: `ce1d3be` (mmu.h, mmu.cpp).

### V12-MEM-02 — ContentionModel state lost on save/load round-trip

* **Classification**: (b) — load_state observable divergence
* **VHDL oracle**:
  - `zxnext.vhd:5786-5828` — NR 0x07 cpu_speed shadow (line 5789,
    latched immediately on write) and effective (line 5817, committed
    on bus-idle CLK_CPU edge). Both flip-flops persist across any
    non-reset edge.
  - `zxnext.vhd:5800-5823` — NR 0x08 bit 6 nr_08_contention_disable
    shadow (line 5805) and eff_nr_08_contention_disable effective
    (line 5823, committed on bus-idle hc(8)='1' edge).
  - `zxnext.vhd:1099-1103 / :2399 / :2594` — NR 0x82 bit 1
    port_7ffd_io_en gate driving `port_7ffd_active` OR-term in
    `port_contend` (:4496).
  - `zxnext.vhd:5906` — NR 0x08 read returns
    `(NOT port_7ffd_locked) & eff_nr_08_contention_disable & ...`
* **Root cause**: `ContentionModel` is intentionally NOT in the
  `save_state` stream (the comment block at emulator.cpp:6058-6073 lists
  it under "rebuilt from config.type by build()"). However, several
  fields owned by ContentionModel are derived from NextReg state and
  were re-pushed by the NR 0x07 / NR 0x08 / NR 0x82 write handlers but
  NEVER replayed on `load_state`:
  - `cpu_speed_` and `pending_cpu_speed_` (NR 0x07 bits 1:0)
  - `contention_disable_` and `contention_disable_shadow_` (NR 0x08 b6)
  - `port_7ffd_io_en_` (NR 0x82 b1)

  Mmu's `contention_disabled_` was saved/loaded — but the read-back
  handler at emulator.cpp:3627 reads `contention_.contention_disable()`
  (the EFFECTIVE field on ContentionModel), which reverted to false
  on load. Result: NR 0x08 readback returned bit 6 = 0 even when the
  saved snapshot had bit 6 = 1; memory contention behaviour also
  diverged.
* **Fix**: In `Emulator::load_state`, after Mmu / NextReg / DivMmc
  re-sync, re-push the four gates from canonical loaded state:
  ```cpp
  const uint8_t cs07 = nextreg_.cached(0x07) & 0x03;
  contention_.set_cpu_speed(cs07);
  contention_.set_pending_cpu_speed(cs07);
  contention_.set_contention_disable(mmu_.contention_disabled());
  contention_.set_port_7ffd_io_en((nextreg_.cached(0x82) & 0x02) != 0);
  ```
  Mirrors the existing `divmmc_.set_rom3_active(mmu_.sram_rom3())` /
  `spi_.set_flash_cs_enable(...)` pattern.
* **Regression test**: `V12-MEM-02-A..D` in
  `test/mmu/mmu_integration_test.cpp:test_contention_state_round_trip`.
  Discriminative: set NR 0x08 bit 6 + commit; save; load on a fresh
  emulator; read NR 0x08 — must return bit 6 = 1, and
  `ContentionModel.contention_disable()` must be true.
* **Commit**: `ce1d3be` (emulator.cpp, mmu_integration_test.cpp).

### V12-MEM-03 — ContentionModel.type_ not refreshed on load

* **Classification**: (b) — load_state observable divergence
* **VHDL oracle**:
  - `zxnext.vhd:5741-5757` — `machine_type_48 / machine_type_128 /
    machine_type_p3` derived combinationally from `nr_03_machine_type`.
  - `zxnext.vhd:4489-4493` — mem_contend per-machine bank decode.
  - `zxnext.vhd:4490-4492` — 48K → page(3:1)="101"; 128K → page(1)='1';
    +3 → page(3)='1'.
* **Root cause**: A snapshot taken after an NR 0x03 commit changed
  `mmu_.machine_type_` (e.g. to ZX48K) and loaded onto a fresh
  Emulator initialised with `EmulatorConfig.type=ZXN_ISSUE2` left
  `ContentionModel.type_` pinned to ZXN_ISSUE2 because
  `ContentionModel::build()` is only called at `Emulator::init()` time.
  Mmu's `machine_type_` was round-tripped (saved/loaded), but the
  derived `ContentionModel.type_` (and its rebuilt LUT) was not.
  Consequence: `is_contended_access()` and `contention_tick()` would
  use the wrong per-machine bank decode (ZXN_ISSUE2's "no contention"
  branch instead of the saved type's actual decode).
* **Fix**: In `Emulator::load_state`, call
  `contention_.rebuild_for_type(mmu_.machine_type())` before re-pushing
  the dynamic gate state. `rebuild_for_type()` was added in Verify9 for
  exactly this kind of runtime type change and preserves all dynamic
  gate state (cpu_speed, contention_disable, mem_active_page, shadows).
* **Regression test**: `V12-MEM-03-A..B` in
  `test/mmu/mmu_integration_test.cpp:test_machine_type_round_trip`.
  Asserts that `Mmu.machine_type()` round-trips AND that the
  ContentionModel decode is consistent with the saved type post-load.
* **Commit**: `ce1d3be` (emulator.cpp).

## Areas specifically scrutinised (no findings)

- NR 0x8E bit-3 / bit-2 update gates for 7FFD / DFFD / 1FFD —
  every combination of bit-3 / bit-2 / bit-0 traced, including the
  port_memory_ram_change_dly suppression and the special-mode entry/
  exit/stay paths. All match VHDL :3640-3742, :4619-4684.
- port_1ffd_special_old transient vs. sticky storage — jnext models
  it as sticky (set after each apply), VHDL as transient (reset to 0
  in the port_1ffd process else branch). Output (MMU image) is identical
  in every traced scenario; the difference is hidden behind the
  arbiter.
- Pentagon-1024 lock-override interaction with port_7ffd / port_dffd /
  port_1ffd writes — `effective_paging_locked()` correctly composes
  `paging_locked_ && !pentagon_1024_en()` per VHDL :3769.
- Layer 2 read/write-over priority cascade (boot ROM > MF > DivMMC > L2
  > altrom > config_mode > regular) and the `sram_pre_override(1)` gate
  per cpu_a half — verified against VHDL :3036-3066, :3077, :3084-3132.
- L2 segment field semantics: low half always enabled (non-MF), high
  half only when seg="11", 0xC000+ never. `l2_overlay_active_for()` /
  `l2_offset_pre_for()` match VHDL :3065 / :2966.
- L2 `layer2_A21_A13(8)` gate (sum bits[6:4]=111 → SRAM inactive,
  read returns floating bus, write dropped) — verify10 fix matches
  VHDL :2971 + :3101-3102.
- mmu_A21_A13 formula `("0001" + page(7:5)) & page(4:0)` — `to_sram_page()`
  faithfully implements this with the bank-5 (0x0A/0x0B) and bank-7-low
  (0x0E) carve-outs per VHDL :2961-2962.
- mem_contend per-page decode — `mem_contend_for_()` mirrors VHDL
  :4489-4493 exactly using `nr_mmu_[]` (= `mem_active_page`) and
  per-machine bank gate.
- nr_8c lo→hi nibble copy on reset (VHDL :2253-2256) — implemented in
  Mmu::reset.
- port_eff7 lock-bypass (VHDL :3780 has no port_7ffd_locked gate) —
  jnext write_port_eff7 always proceeds.
- NR 0x08 bit 7 paging-unlock — clears port_7ffd_reg(5) AND
  paging_locked_ without triggering port_memory_change_dly.
- altrom address override (`altrom_sram_page_`) per machine type
  (VHDL :2986/:2988-2995/:2998-3005) — verified.
- to_sram_page wrap behaviour for high pages (0xE0..0xFF → 0x00..0x1F)
  matches VHDL 9-bit add wraparound.
- NR 0x50/0x51 with v in [0xE0..0xFE] high-page → routes through legacy
  ROM via rebuild_ptr's high-page slot 0/1 branch; nr_mmu_ keeps
  verbatim, slots_ holds physical (Verify11 fix).
- compose_bank_ Pentagon vs non-Pentagon branches and bank(5)/bank(6)
  derivation per VHDL :3763-3766.
- save_state / load_state schema: every persisted Mmu field traced
  against VHDL register surface. Found 2 missing seams (V12-MEM-02 +
  V12-MEM-03), both fixed.

## Test results

```
ctest --test-dir build --output-on-failure
  100% tests passed, 0 tests failed out of 38
mmu_integration_test
  Total: 11 Passed: 11 Failed: 0 Skipped: 0
fuse_z80_test build/test/fuse
  Total: 1356  Passed: 1356  Failed: 0  Skipped: 0
```

## Build mode

CMake Release (`-DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`). All tests
pass under Release optimisation.

## Fix-of-reviewer follow-ups (post-`635ddbc`)

Reviewer (`task2/verify12-memory-reviewer` HEAD `635ddbc`) returned
APPROVE-WITH-NITS. Two NITs addressed on this branch:

### NIT-1 — V12-MEM-03-B is now discriminative

The published V12-MEM-03-B was non-discriminative: the live emu was
`ZXN_ISSUE2`, so the saved `machine_type` was `ZXN_ISSUE2`, and
`is_contended_access()` short-circuits to `false` at
`contention.cpp:31` (`if (type == ZXN_ISSUE2) return;`) regardless of
whether `rebuild_for_type` was called from `load_state`. The test
passed even with the V12-MEM-03 fix reverted, masking the bug.

V12-MEM-03-B has been replaced with the reviewer's discriminative form:

1. Switch the live emu's `Mmu.machine_type` to `ZX48K` via
   `Mmu::set_machine_type` (the same code path NR 0x03 typ_sel commits
   use).
2. Save state.
3. Load onto a fresh emu initialised at `ZXN_ISSUE2`.
4. Set `mem_active_page=0x0A` (bits[3:1]=101 = bank 5, contended on 48K
   per VHDL :4490).
5. Assert `fresh.contention().is_contended_access() == true`.

Pre-Verify12 behaviour: fresh ContentionModel.type_ stays at
`ZXN_ISSUE2` (init-time value) → `is_contended_access()` returns
`false` → V12-MEM-03-B FAILS. Post-fix:
`rebuild_for_type(mmu_.machine_type())` flips type_ to `ZX48K` →
`is_contended_access()` returns `true` → V12-MEM-03-B PASSES.

V12-MEM-03-A is also strengthened: it now asserts
`Mmu.machine_type() == ZX48K` post-load (was `== saved_mt` from a live
emu still at `ZXN_ISSUE2`), guarding against any future regression of
the underlying Mmu serialisation when the saved type differs from the
fresh emu's init type.

The live emu's original machine_type is restored at the end of the
test to keep it isolated from anything that runs after.

**Discriminative verification done in this branch**: with the V12-MEM-03
fix line at `emulator.cpp:6292` commented out, the rebuild produces:

```
FAIL V12-MEM-03-B: ContentionModel.type_ tracks Mmu.machine_type()
  across load_state — ZX48K + page=0x0A (bank 5) contends [...] [
  expected ZX48K bank-5 → contended; got is_contended=0
  (ContentionModel.type_ likely still ZXN_ISSUE2 — rebuild_for_type
  missing from load_state)]
Total: 11 Passed: 10 Failed: 1
```

With the fix restored, all 11/11 pass.

### NIT-2 — V12-MEM-01-A test isolation

V12-MEM-01-A wrote `NR 0x50 = 0xE5` (high-page legacy-ROM trigger) and
left it. Subsequent tests didn't depend on NR 0x50, so this was benign,
but it violated test-isolation hygiene. Fixed by writing `NR 0x50 = 0xFF`
at the end of the test — the legacy "engage auto-paging" sentinel
(VHDL :4611-4612) — restoring slot 0 to its boot-time mapping and
discarding the verbatim 0xE5.

### Tests after fix-of-reviewer

```
ctest --test-dir build --output-on-failure
  100% tests passed, 0 tests failed out of 38
mmu_integration_test
  Total: 11 Passed: 11 Failed: 0 Skipped: 0
  Per-group: EF7-IO-EN 3/3, V12-MEM-01-NR8C 2/2,
             V12-MEM-02-CONT 4/4, V12-MEM-03-MT 2/2
fuse_z80_test build/test/fuse
  Total: 1356  Passed: 1356  Failed: 0  Skipped: 0
```
