# Pass-12 Memory subsystem fix-of-reviewer — independent fix-review

**Fix-reviewer branch**: `task2/verify12-memory-fix-reviewer`
(forked off `task2/verify12-memory` at HEAD `cc4ffc8`)
**Worktree**: `.claude/worktrees/task2-verify12-memory-fix-reviewer`
**Methodology**: blind audit of the two NIT fixes (`64b9858` test fixes
+ `cc4ffc8` doc append), VHDL-as-oracle, no probes, no push, no PR.
Release-mode build.

## Verdict

**APPROVE**.

Both NIT fixes are correct, surgical, well-cited against the VHDL oracle,
and the discriminative protocol holds: with the V12-MEM-03 fix at
`emulator.cpp:6292` reverted, the new V12-MEM-03-B FAILs as predicted;
restoring the fix yields 11/11 pass. NIT-2's `nr_write(emu, 0x50, 0xFF)`
restoration correctly mirrors the VHDL :4611-4612 boot-time default.
No regressions in the full Release-mode test suite.

## NIT-1 verified — V12-MEM-03 disc-test replacement

### What changed

Fix-of-reviewer replaced the audit's non-discriminative V12-MEM-03-B (live
emu was `ZXN_ISSUE2`, saved type was `ZXN_ISSUE2`, decode short-circuits
to `false` at `contention.cpp:31` — passed regardless of fix presence)
with the reviewer's discriminative form:

1. Stash live emu's original `Mmu.machine_type_` (= `ZXN_ISSUE2`).
2. Switch live emu to `ZX48K` via `Mmu::set_machine_type(ZX48K)` (same
   path NR 0x03 typ_sel commits use, `mmu.h:803`).
3. Save state.
4. Construct fresh emulator, init to `ZXN_ISSUE2`, then `load_state(...)`.
5. Set `fresh.contention().set_mem_active_page(0x0A)`.
6. Assert `fresh.contention().is_contended_access() == true`.
7. Restore live emu's `Mmu.machine_type_` via `set_machine_type(original)`.

V12-MEM-03-A was also strengthened: now asserts `Mmu.machine_type() == ZX48K`
post-load (was `== saved_mt` from a live-emu still at `ZXN_ISSUE2`),
guarding against any future regression of Mmu's `machine_type_`
serialisation when saved type differs from fresh-emu init type.

### VHDL oracle re-verified

* `zxnext.vhd:4490` — `mem_contend = '1' when machine_timing_48 = '1'
  and mem_active_page(3:1) = "101"`. Bank 5 only. **Confirmed.**
* `0x0A` decoded: `mem_active_page = 0x0A` → `(0x0A >> 1) & 0x07 = 5 =
  "101"` → contended on 48K. **Confirmed.**
* `contention.cpp:79-126` `is_contended_access()` short-circuit chain:
  - `if (contention_disable_) return false;` (line 105 — fresh emu
    starts with `contention_disable_=false`, which the V12-MEM-02 fix
    re-pushes correctly post-load).
  - `if (cpu_speed_ != 0) return false;` (line 106 — re-pushed from
    `nextreg_.cached(0x07) & 0x03 = 0` for a freshly init'd emu loading
    a snapshot taken with NR 0x07 unchanged).
  - `if ((mem_active_page_ & 0xF0) != 0) return false;` (line 109 —
    `0x0A & 0xF0 = 0`, gate open).
  - `case ZX48K: return ((low >> 1) & 0x07) == 0x05;` (line 115 — `0x0A`
    matches → returns `true`).
* The ZXN_ISSUE2 short-circuit at `contention.cpp:31` (which made the
  audit's V12-MEM-03-B non-discriminative) is in the `rebuild_for_type`
  helper at line 31, NOT in `is_contended_access`. The
  `rebuild_for_type(ZXN_ISSUE2)` call from `init` returns early; the
  decode at `is_contended_access` falls through `case ZXN_ISSUE2: return
  false` at line 124. So pre-fix (no `rebuild_for_type` in load_state):
  fresh `type_` stays `ZXN_ISSUE2` → `is_contended_access()` returns
  `false`. Post-fix: `type_` flips to `ZX48K` → returns `true`. **Test
  is genuinely discriminative.**

### Discriminative protocol confirmed

Step (per task): comment out `contention_.rebuild_for_type(mmu_.machine_type())`
at `emulator.cpp:6292`, rebuild, run.

```bash
sed-equivalent edit on emulator.cpp:6292 → commented out the call
cmake --build build -j --target mmu_integration_test
./build/test/mmu_integration_test
```

Result with fix REVERTED:
```
FAIL V12-MEM-03-B: ContentionModel.type_ tracks Mmu.machine_type()
  across load_state — ZX48K + page=0x0A (bank 5) contends [...]
  [expected ZX48K bank-5 → contended; got is_contended=0
   (ContentionModel.type_ likely still ZXN_ISSUE2 — rebuild_for_type
   missing from load_state)]
Total: 11 Passed: 10 Failed: 1
```

`git checkout HEAD -- src/core/emulator.cpp` to restore. Rebuild. Result
with fix RESTORED:
```
Total: 11 Passed: 11 Failed: 0 Skipped: 0
  V12-MEM-03-MT          2/2
```

**Discriminative protocol verified.** The new V12-MEM-03-B is a real
regression test: it fails when the fix is missing, passes when present.

### Test isolation hygiene

The new test stashes `original_mt = emu.mmu().machine_type()` and
restores it via `emu.mmu().set_machine_type(original_mt)` at the end.
`set_machine_type(ZX48K)` early-returns if `t == machine_type_` (it
isn't), then traverses the +3 special-paging guard (port_1ffd_ & 0x01 ==
0 for the live emu in default state) and refreshes the legacy-ROM
read-pointer cache for slots 0/1 with `set_nr_sentinel=false` (preserving
`nr_mmu_[]`). The reverse call back to `ZXN_ISSUE2` does the same
correctly. `set_mem_active_page(0x0A)` is on the `fresh` (local-scope)
emu only, so no leak. No Mmu / NextReg / Contention state leaks past
the test boundary.

V12-MEM-03 is the last test in `main()`, so any residual state leak
would not affect anything anyway — but the restore is correct hygiene.

## NIT-2 verified — V12-MEM-01-A test isolation

### What changed

`test_nr_8c_preserves_nr_mmu` now writes `nr_write(emu, 0x50, 0xFF)`
at the end of the test, restoring slot 0 to the boot-time mapping
(MMU0=0xFF, the legacy auto-paging sentinel).

### VHDL oracle re-verified

* `zxnext.vhd:4611-4612` — On `reset='1'`, `MMU0 <= X"FF"`, `MMU1 <=
  X"FF"`. **Confirmed boot default is 0xFF.**
* `zxnext.vhd:4686-4699` — `nr_mmu_we='1'` stores `nr_wr_dat` verbatim
  into MMU<i> via the NR 0x50..0x57 case. Writing NR 0x50=0xFF stores
  0xFF verbatim, matching the reset-time value. **Confirmed.**
* The 0xFF value is interpreted by the SRAM arbiter at
  `zxnext.vhd:3037-3057`: `mmu_A21_A13(8)='1'` (since `(0x1FF + 0xE0) >>
  8 = 1`... actually 0xFF = high page → routes through legacy ROM via
  `sram_rom`). **NR 0x50=0xFF correctly engages legacy auto-paging.**

The NIT-2 fix mirrors what reset would do, restoring MMU0 to its
constructor default. The `nr_write(emu, 0x50, 0xFF)` path goes through
`engage_legacy_rom_paging_slot(0, set_nr_sentinel=true)` (the NR $50/$51
dispatcher at `emulator.cpp:1586`), which correctly stores `nr_mmu_[0] =
0xFF` per VHDL :4686 + :3813 (the dispatcher path is the one VHDL
intends to set MMU<i> to whatever `nr_wr_dat` is, including the 0xFF
sentinel).

### Test still passes

```
Per-group breakdown:
  V12-MEM-01-NR8C        2/2
```

Both V12-MEM-01-A (`pre == 0xE5`) and V12-MEM-01-B (`post_8c == 0xE5`)
still pass — the NIT-2 fix only adds a final `nr_write(emu, 0x50, 0xFF)`
AFTER all V12-MEM-01 assertions, so it cannot affect the assertions'
outcome. **Confirmed.**

### Hygiene benefit

V12-MEM-01-A leaves the live emu's MMU register surface clean (NR 0x50
back to boot default, NR 0x8C already restored to `prev_8c`). Any
subsequent test running on the same `emu` (V12-MEM-02-CONT,
V12-MEM-03-MT) starts from a clean MMU register surface — V12-MEM-02
doesn't touch slot 0/1 MMU state but the hygiene is consistent with the
"restore initial NR" pattern applied throughout the file (V12-MEM-02
restores NR 0x08; V12-MEM-03 restores `machine_type_`).

## Hunt for missed cases — non-discriminative-shape checks

I spot-checked the four other test groups in `mmu_integration_test.cpp`
for the same non-discriminative shape that the audit's V12-MEM-03-B had:

### EF7-IO-EN-{00,01,02} (lines 133-197)

**Discriminative.** Tests are a paired set: same write to 0xEFF7
(0x0C = b2|b3) with NR 0x85 b2 set vs cleared. Per VHDL `port_eff7 <=
port_eff7_lsb AND port_eff7_io_en`, the gate-closed write must be
dropped (state stays false), the gate-open write must succeed (both
flags set true). The two rows would yield different outcomes if the
gate logic were broken on either side. **No issue.**

### V12-MEM-01-{A,B} (lines 224-263)

**Discriminative.** Pair: write NR 0x50=0xE5 → read 0xE5 (V12-MEM-01-A);
NR 0x8C lock-bits flip → re-read NR 0x50, must still be 0xE5 (V12-MEM-01-B).
Pre-fix: V12-MEM-01-B fails because `set_nr_8c` clobbered `nr_mmu_[0]`
to 0xFF. The audit confirms this was reverter-checked. **No issue.**

### V12-MEM-02-{A,B,C,D} (lines 289-366)

**Discriminative.** Set NR 0x08 b6 + force `commit_contention_disable_on_hc`
+ save → load on fresh emu → assert b6=1 (V12-MEM-02-C) AND
`fresh.contention().contention_disable() == true` (V12-MEM-02-D).
Pre-fix: ContentionModel `contention_disable_` resets to `false` (init
default), V12-MEM-02-C fails (NR 0x08 read returns b6=0), V12-MEM-02-D
fails. The audit confirms this was reverter-checked. **No issue.**

### V12-MEM-03-{A,B} (lines 403-494)

**Discriminative now.** V12-MEM-03-A: live emu switched to ZX48K → save →
fresh ZXN init → load → assert `machine_type == ZX48K`. V12-MEM-03-B:
fresh load → set `mem_active_page=0x0A` → assert `is_contended_access()
== true`. Both confirmed via revert protocol above. **No issue.**

## Hunt for missed cases — state-leak checks

I scanned every test group for state writes that aren't restored at the
end (`prev_X = nr_read(); nr_write(prev_X);` pattern):

### EF7-IO-EN

**Pre-existing minor leak.** Test leaves NR 0x85 b2=1 (gate open) and
EFF7=0x0C (port_eff7_reg_2_=true, port_eff7_reg_3_=true). The subsequent
V12-MEM-01 test writes NR 0x50=0xE5 via the `nr_mmu_we` path; per VHDL
:4636 the eff7_reg_3=1 override would only fire on the next
`port_memory_change_dly` pulse (i.e. on a port_7FFD/1FFD/DFFD/EFF7 / NR
8E / NR 8F write — none happen in V12-MEM-01). NR 0x50 verbatim storage
is independent of port_eff7 state. **Leak is benign for the current
test sequence**, but flag for hygiene improvement: the EFF7-IO-EN test
should restore NR 0x85 + EFF7 to their pre-test values. Out of scope
for V12-MEM fix-reviewer (pre-existing, not introduced by NIT fixes).

### V12-MEM-01

**Restored cleanly.** NR 0x8C → `prev_8c`. NR 0x50 → 0xFF (the NIT-2
fix). **No leak.**

### V12-MEM-02

**Restored cleanly.** NR 0x08 → `initial_08`. ContentionModel state on
the live emu is already at default for `contention_disable_` and gets
re-committed on the next `commit_contention_disable_on_hc` cycle if the
test left it in shadow≠effective. The `commit_contention_disable_on_hc(0x100)`
call line 299 explicitly drives the commit before save, so the live emu
has shadow==effective==true post-test. The restore at line 365 writes
NR 0x08 back to `initial_08` (b6=0) which clears the SHADOW; the
EFFECTIVE field stays true until the next `commit_contention_disable_on_hc`
cycle — but no subsequent test calls `is_contended_access()` on the live
`emu` so this is benign. Not a real leak in practice. (The test is V12-MEM
last-but-one; only V12-MEM-03-MT runs after, and V12-MEM-03-MT does NOT
read live-emu's contention state — it only saves/loads onto a fresh
emu.) **No issue.**

### V12-MEM-03

**Restored cleanly.** `machine_type_` → `original_mt`. **No leak.**

### Summary of hunt

- The two NIT fixes themselves introduce zero new leaks.
- The NIT-2 fix (NR 0x50=0xFF restore) actively closes a benign-but-real
  leak.
- One pre-existing minor leak found (EFF7-IO-EN doesn't restore NR 0x85
  + EFF7), benign for the current test ordering, flag for future
  housekeeping. Out of scope for V12-MEM fix-reviewer.

No new findings to escalate.

## Test results

### Build

```
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON
  → Configuring done (7.0s)
  → Build files generated.
cmake --build build -j$(nproc)
  → [100%] Built target jnext
```

Clean build (after copying `roms/nextboot.rom` into the worktree —
private-artefact, not a fix issue).

### Suite results post-fix-restored

```
ctest --test-dir build --output-on-failure
  100% tests passed, 0 tests failed out of 38

mmu_integration_test
  Total: 11 Passed: 11 Failed: 0 Skipped: 0
  Per-group breakdown:
    EF7-IO-EN              3/3
    V12-MEM-01-NR8C        2/2
    V12-MEM-02-CONT        4/4
    V12-MEM-03-MT          2/2

fuse_z80_test build/test/fuse
  Total: 1356  Passed: 1356  Failed: 0  Skipped: 0
```

### Discriminative-revert verification (ad-hoc, not committed)

`emulator.cpp:6292` line `contention_.rebuild_for_type(mmu_.machine_type());`
commented out, mmu_integration_test rebuilt + run:
```
FAIL V12-MEM-03-B: ContentionModel.type_ tracks Mmu.machine_type() across
  load_state — ZX48K + page=0x0A (bank 5) contends [...]
Total: 11 Passed: 10 Failed: 1
```
Restored via `git checkout HEAD -- src/core/emulator.cpp`, rebuilt:
```
Total: 11 Passed: 11 Failed: 0 Skipped: 0
```

**Discriminative protocol verified.**

## Issues / NITs

None. Both fix-of-reviewer NITs are correct, well-cited, well-tested,
and don't introduce new issues.

## Final verdict

**APPROVE**. Both NIT fixes verified, no new issues, full Release-mode
test suite passes (38/38 ctest, 11/11 mmu_integration, 1356/1356 FUSE
Z80).
