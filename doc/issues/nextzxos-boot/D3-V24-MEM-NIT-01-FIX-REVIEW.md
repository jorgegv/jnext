# D3 — V24-MEM-NIT-01 Fix-of-Reviewer Review

**Reviewer:** independent (post-merge, post-fix-of-reviewer)
**Subject commits:**
- `2bad72ca` — V24-MEM-NIT-02 (doc-only, SKIPPED per workflow rule
  `feedback_task2_skip_review_comment_only.md`)
- `f448aa81` — V24-MEM-NIT-01 (code fix subject of this review)

**Branch HEAD reviewed:** `f448aa81bc40892d87b45a50374ae53e80a27b7b`
**Verdict:** **APPROVE — no missed concerns**

---

## 1. Subject of the fix

V24-MEM-NIT-01 (reviewer follow-up on the V24-MEM-01 contention timing
axis split, commit `6e68c680`) addresses a one-frame round-trip
imperfection: pre-fix `Emulator::load_state` at emulator.cpp:7376-7395
unconditionally called `set_machine_timing(tim_mode)` on BOTH
`ContentionModel` and `Mmu` after `Mmu::load_state` had already restored
the appended schema slots — collapsing the `(effective, pending)` pair
into the NextReg-derived effective value. A snapshot captured between
an NR 0x03 bits-6:4 write and the next video-frame edge would therefore
round-trip the *pending* value as immediately effective.

VHDL anchor (zxnext.vhd:6694-6703):

```vhdl
process (i_CLK_7)
begin
   if rising_edge(i_CLK_7) then
      if video_frame_sync = '1' then
         ...
         eff_nr_03_machine_timing <= nr_03_machine_timing;
         ...
```

The latch `eff_nr_03_machine_timing <= nr_03_machine_timing` ONLY fires
on `video_frame_sync='1'`. Between an NR 0x03 bits-6:4 write and that
frame edge the two fields may differ — and a save_state at that point
must round-trip both halves of the pair.

## 2. The fix (commit `f448aa81`)

### 2.1 Mmu (src/memory/mmu.{h,cpp})

- New private transient flag `machine_timing_loaded_from_schema_`
  (default `false`, NOT serialised).
- Public accessor `bool machine_timing_loaded_from_schema() const`.
- `Mmu::load_state` resets the flag at entry, then sets it true iff
  BOTH `!r.eof()`-guarded schema reads succeed:

```cpp
machine_timing_loaded_from_schema_ = false;        // entry reset
...
bool got_machine_timing = false;
bool got_pending        = false;
if (!r.eof()) { machine_timing_ = ...; got_machine_timing = true; }
if (!r.eof()) { pending_machine_timing_ = ...; got_pending = true; }
machine_timing_loaded_from_schema_ = got_machine_timing && got_pending;
```

### 2.2 Emulator (src/core/emulator.cpp:7376-7419)

The post-Mmu re-sync now branches on the flag:

- **Path (1) flag=true (new-format save):** trust the Mmu schema-restored
  pair. Push the pair into ContentionModel via:

```cpp
const MachineTimingMode eff_tim  = mmu_.machine_timing();
const MachineTimingMode pend_tim = mmu_.pending_machine_timing();
contention_.set_pending_machine_timing(eff_tim);
contention_.commit_pending_machine_timing();
contention_.set_pending_machine_timing(pend_tim);
```

After step 1: CM `(eff, pend) = (?, eff_tim)`.
After step 2: CM `(eff, pend) = (eff_tim, eff_tim)`.
After step 3: CM `(eff, pend) = (eff_tim, pend_tim)`.

- **Path (2) flag=false (old-format save):** unchanged from pre-fix —
  re-derive from `nextreg_.nr_03_machine_timing()` and call
  `contention_.set_machine_timing(tim_mode)` + `mmu_.set_machine_timing(tim_mode)`
  (immediate-commit setter that collapses both fields).

### 2.3 Test (test/contention/contention_test.cpp)

New `D3-CONTENTION-NIT-01` discriminative test:
1. Init Emulator(ZX48K) — both axes = `Timing48`.
2. Write NR 0x03 = 0xA0 — pending advances to `Timing128`, effective
   stays `Timing48`.
3. Save state.
4. Construct second Emulator(ZX48K). Plant divergence on its CM
   pending (`TimingPlus3`).
5. Load state. Verify Mmu AND CM both show `(eff=Timing48, pend=Timing128)`.
6. Verify `mmu_.machine_timing_loaded_from_schema()` returns true.
7. Run frame. Verify both promote to `effective=Timing128`.

## 3. VHDL alignment

`video_frame_sync` is the **only** trigger for the
`eff_nr_03_machine_timing <= nr_03_machine_timing` latch (VHDL :6694-6703).
A state-load is not a clock edge in the FPGA model — it's a snapshot
restore. The correct semantic is therefore:

- After load, BOTH `nr_03_machine_timing` (pending) and
  `eff_nr_03_machine_timing` (effective) must equal the saved values.
- The NEXT `video_frame_sync` then promotes pending → effective in the
  normal way.

The fix's Path (1) achieves this exactly. The 3-step
`set_pending(eff) + commit_pending + set_pending(pend)` dance is
**not** an unwanted commit — `commit_pending_machine_timing()` is a
purely synchronous host-side assignment with NO side effects (no LUT
rebuild, no contention recompute, no observer hook), and it is called
between the two `set_pending` calls that bracket it. The transient
state `(pending=eff_tim, effective=eff_tim)` between step 2 and step 3
exists for at most a few instructions inside `load_state`, never
observable. After step 3 the CM state matches the Mmu state exactly:
`(effective=eff_tim, pending=pend_tim)`. The first `run_frame()` after
load then performs the canonical `commit_pending_machine_timing()` at
the per-frame seam (mirroring VHDL `video_frame_sync='1'`), which
promotes the pending value to effective as required.

**VHDL alignment: CORRECT.**

## 4. Sandwich verification (independent)

Configuration: cmake Release, ENABLE_QT_UI=ON, host build.

| Step | Description | Result |
|------|-------------|--------|
| Baseline | Build with fix applied | `contention_test: 89/89 PASS` |
| Revert  | Replace `if (mmu_.machine_timing_loaded_from_schema()) { … } else { … }` with the pre-fix unconditional `set_machine_timing(decode_nr_03_machine_timing(nextreg_.nr_03_machine_timing()))` on BOTH `contention_` and `mmu_` | `contention_test: 88/89` |
| Failure detail | D3-CONTENTION-NIT-01 reports `load(mmu_eff,mmu_pend,cm_eff,cm_pend)=(1,1,1,1) expected(0,1,0,1); schema_flag=1; post-frame(mmu_eff,cm_eff)=(1,1)` | matches commit-message claim exactly |
| Restore | Revert the revert | `contention_test: 89/89 PASS` |

**The test FAILS in exactly the cell the fix targets** (load-time
collapse of pending into effective). The src state pre-save is correct
`(0,1,0,1)`, the post-frame state is correct `(1,1)` (commit_pending
fires once run_frame is invoked, regardless of fix), but the post-load
state without the fix is `(1,1,1,1)` — both Mmu and ContentionModel
collapsed to effective. With the fix restored the post-load state is
the expected `(0,1,0,1)`.

**Sandwich: discriminative.**

## 5. Backward-compatibility (OLD-format saves)

Verified by code analysis (no pre-V24 binary save artefacts available,
but the path is small enough to audit).

**Old-format flow:**
1. `Mmu::load_state` entry → `machine_timing_loaded_from_schema_ = false`.
2. All pre-V24 fields read out of stream up to the position where the
   old save_state stopped writing.
3. `!r.eof()` returns FALSE (stream exhausted exactly at the legacy
   tail — StateReader::eof returns `pos_ >= capacity_`).
4. Both `!r.eof()` guards short-circuit. `got_machine_timing` and
   `got_pending` stay `false`. Flag stays `false`.
5. `machine_timing_` and `pending_machine_timing_` retain the
   constructor default `TimingPlus3` (VHDL :1099/:1377).
6. `Emulator::load_state` Path (2) executes:
   - Decodes `nextreg_.nr_03_machine_timing()` → `tim_mode`.
   - Calls `contention_.set_machine_timing(tim_mode)` (immediate-commit:
     both fields = `tim_mode`).
   - Calls `mmu_.set_machine_timing(tim_mode)` (immediate-commit: both
     fields = `tim_mode`).
7. Result: both Mmu and CM `(effective=pending=tim_mode)` — byte-for-byte
   identical to pre-fix behaviour.

**Asymmetric short-tail case (defensive analysis):** if a malformed
save contains only the first `machine_timing_` slot but not the second:
- `got_machine_timing = true`, `got_pending = false`.
- Flag = `true AND false = false`.
- Path (2) executes. The half-read `machine_timing_` field is
  overwritten by `mmu_.set_machine_timing(tim_mode)`. Safe — no torn
  state propagates.

**Backward-compat: PRESERVED.**

## 6. Side-effect inspection

| Concern | Verdict |
|---------|---------|
| 3-step sequence transiently breaks `(effective, pending)` invariant? | `commit_pending_machine_timing()` is a single synchronous assignment with no side effects (no LUT rebuild, no contention recompute, no observer fan-out). The transient state between step 2 and step 3 — `(eff=eff_tim, pending=eff_tim)` — exists only within the synchronous `load_state` call; no observer between subsystem load_state calls. **No side effect.** |
| Multi-tick / partial-frame load? | `load_state` is called atomically (whole snapshot consumed before any `run_frame` resumes). Not a concern. |
| save/load symmetry? | save_state writes `(machine_timing_, pending_machine_timing_)` in that order at mmu.cpp:890-891. load_state reads the same pair in the same order at mmu.cpp:970-975. **Symmetric.** |
| Flag NOT serialised? | Verified — flag is a transient one-shot signal scoped to a single `load_state` invocation. Comment at mmu.h:884-887 documents intent. Mirrors `V16-CPU-01`-style derived-shadow re-push pattern. |
| Adjacent flags needed for CM? | ContentionModel is NOT serialised at all (no save_state/load_state). It is a fully-derived shadow surface re-pushed from canonical Mmu/NextReg state at the end of `Emulator::load_state`. Mmu is the canonical schema source for the timing axis. **Pattern is consistent — no additional flag needed.** |
| `set_pending_machine_timing` semantics? | Per contention.h:268-270, sets only the shadow field `pending_machine_timing_`. No side effects. Path (1)'s 3-step dance correctly leaves CM at `(eff_tim, pend_tim)` after completion. |

## 7. Test invariants (all PASS post-fix)

| Suite | Result |
|-------|--------|
| ctest (38 targets) | `100% tests passed, 0 tests failed out of 38` |
| FUSE Z80 | `Total: 1356  Passed: 1356  Failed: 0  Skipped: 0` |
| regression | `Pass: 33  Fail: 0  Skip: 0` |
| contention_test | `Total: 89  Passed: 89  Failed: 0  Skipped: 0` (+1 vs pre-fix 88) |

## 8. Comment-only commit `2bad72ca`

V24-MEM-NIT-02 is a doc-only comment touch-up to `contention.h`
clarifying the post-fix consumer set of `machine_timing_`. Per
workflow rule `feedback_task2_skip_review_comment_only.md`,
comment-only fix-of-reviewer commits skip the fix-reviewer step.
**SKIPPED.**

## 9. Final verdict

**APPROVE — no missed concerns.**

Rationale:
- VHDL-aligned: `eff_nr_03_machine_timing <= nr_03_machine_timing` on
  `video_frame_sync` only; load does not synthesise a commit; Path (1)
  preserves the deferred-commit pair as required.
- Sandwich discriminative: revert → FAIL `load(1,1,1,1) expected(0,1,0,1)`;
  restore → PASS — matches commit-message claim and isolates the
  responsibility of the new code path.
- Backward-compat preserved: Path (2) is byte-for-byte the pre-fix
  behaviour for old-format saves; flag mechanism is robust against
  short-tail / interrupted saves.
- No side effects: 3-step sequence is observably equivalent to a single
  atomic `(set_effective=eff_tim, set_pending=pend_tim)` assignment.
- Consistent with existing precedent: mirrors `V16-CPU-01`-style
  derived-shadow re-push pattern; ContentionModel is fully-derived
  (not serialised) and is re-pushed from canonical Mmu state.
- All test invariants hold: ctest 38/38, FUSE 1356/1356, regression 33/0/0,
  contention 89/89 (+1 D3-CONTENTION-NIT-01).
