# NEXTZXOS Boot Subsystem — Pass-16 CPU (Z80 + Z80N + IM2) Independent Review

**Branch:** `task2/verify16-cpu-z80n-im2-reviewer` (off `task2/verify16-cpu-z80n-im2` HEAD `ad0a85f`)
**Reviewer audit ref:** `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY16-CPU.md`
**Date:** 2026-05-10
**Mode:** Independent review — read audit report + audit's commits only.

## Verdict

**APPROVE.**

All findings verified. Fix is minimal, correctly placed, doc-comments
accurate, and the discriminative test soundly distinguishes pre-fix
from post-fix. Full Release-mode test suite green: ctest 38/38, FUSE
1356/1356, rewind_test 22/0 (10 SKIP). No additional shadow-propagation
gaps were found in load_state.

## Summary

| Audit class | Audit count | Verified | Reviewer-promoted |
|-------------|-------------|----------|-------------------|
| (a)         | 1           | 1        | 0                 |
| (b)/(c)/(d) | 0           | 0        | 0                 |
| **Total**   | **1**       | **1**    | **0**             |

## VHDL oracle verification (V16-CPU-01)

The audit claims NR 0x85 bit 0 → `port_ulap_io_en` per
`zxnext.vhd:2439`. Confirmed against the FPGA source:

* `zxnext.vhd:458` — `signal internal_port_enable : std_logic_vector(27 downto 0);`
* `zxnext.vhd:1226-1229` —
  ```
  nr_82_internal_port_enable : 8 bits, reset all-1
  nr_83_internal_port_enable : 8 bits, reset all-1
  nr_84_internal_port_enable : 8 bits, reset all-1
  nr_85_internal_port_enable : 4 bits, reset all-1
  ```
* `zxnext.vhd:2392-2393` —
  ```
  internal_port_enable <= (nr_85 & nr_84 & nr_83 & nr_82) ...
  ```
  Therefore bit composition is **bits[7:0]=nr_82, [15:8]=nr_83,
  [23:16]=nr_84, [27:24]=nr_85**. Bit 24 = the first (low) bit of
  `nr_85_internal_port_enable`, i.e. **NR 0x85 bit 0**.
* `zxnext.vhd:2439` — `port_ulap_io_en <= internal_port_enable(24);`
* `zxnext.vhd:2685-2686` — `port_bf3b`/`port_ff3b` AND-gated by
  `port_ulap_io_en`.
* `zxnext.vhd:4496` — `port_contend <= (NOT cpu_a(0)) OR
  port_7ffd_active OR port_bf3b OR port_ff3b;`

Reset value of `nr_85_internal_port_enable` is "all-1" (4 bits) → reset
NR 0x85 bit 0 = 1 → `port_ulap_io_en` defaults TRUE. `nextreg.cpp:43`
confirms `regs_[0x85] = 0x8F` at power-on (bit 0 set among the low 4
bits, plus reset_type bit 7). Audit oracle claim **CORRECT**.

## Fix correctness

### One-line load_state push (emulator.cpp:6534)

```cpp
contention_.set_port_7ffd_io_en((nextreg_.cached(0x82) & 0x02) != 0);
contention_.set_port_ulap_io_en((nextreg_.cached(0x85) & 0x01) != 0);  // V16-CPU-01
```

* **Placement:** correctly adjacent to the existing V12-MEM-02
  `set_port_7ffd_io_en` line, identical pattern. The enclosing block
  comment was extended (8 lines) to document the gap closure with
  cross-references to V15-CPU-NIT-03 and V12-MEM-02. The block calls
  `rebuild_for_type(mmu_.machine_type())` first (preserving dynamic
  gate state per the contention.cpp:20-31 contract) and then re-pushes
  every NR-derived shadow.
* **NextReg load order:** `nextreg_.load_state(r)` is invoked at
  `emulator.cpp:6440`, well before the contention re-push at 6529-6534.
  Therefore `nextreg_.cached(0x85)` is the just-restored value at the
  moment the bit-0 mask runs.
* **MMU load order:** `mmu_.load_state(r)` at 6439, also before the
  re-push, so `mmu_.machine_type()` for `rebuild_for_type` is correct.

### Doc-comment corrections

* **`src/memory/contention.cpp:143`** — was "NR 0x82 bit 8 →
  internal_port_enable(24)". Corrected to "NR 0x85 bit 0 →
  internal_port_enable(24)". Bit 24 cannot be "NR 0x82 bit 8" (NR 0x82
  only has 8 bits; the next byte starts NR 0x83). The corrected
  attribution matches `zxnext.vhd:2392-2393` decomposition.
* **`src/memory/contention.h:177-182`** — was "mirrors NR 0x82 bit 4".
  Corrected to "mirrors NR 0x85 bit 0 (zxnext.vhd:2439)" with a note
  that the model's internal `port_ulap_io_en_` shadow is OR-folded
  with the parameter so CPU-side seams without NR access still see
  the correct gate. **Accurate** — matches the V15-CPU-NIT-03 OR-fold
  semantics in `contention.cpp:contention_tick()`.

Both corrections are pure documentation; no functional change.

## Discriminative test verification

Verified by:

1. Build Release + ENABLE_QT_UI=ON (jnext executable + 38 test exes).
2. **Baseline (fix applied):** `./build/test/rewind_test` → 22/0 PASS,
   10 SKIP. Total 32. Matches audit claim.
3. **Revert** the V16 fix line at `emulator.cpp:6534` (replaced with
   `// REVERT-CHECK: V16-CPU-01 fix removed for discriminative test.`),
   rebuild, re-run.
4. **Post-revert:** `rewind_test` → 20/2 (the 2 FAIL lines are exactly
   the audit-cited ones):
   ```
   FAIL [test/rewind/rewind_test.cpp:374] V16-CPU-01: load_state re-pushes port_ulap_io_en from NR 0x85 b0
   FAIL [test/rewind/rewind_test.cpp:390] post-load contention_tick at $BF3B with default param fires non-zero stretch (V15-CPU-NIT-03 OR-fold sees true shadow)
   ```
5. **Restore** fix, rebuild, re-run → 22/0 again.

Discriminative protocol PASS → FAIL → PASS confirmed.

## Test suite

All in Release mode (`-DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`):

```
ctest --test-dir build:
  100% tests passed, 0 tests failed out of 38

FUSE Z80:
  Total: 1356  Passed: 1356  Failed: 0  Skipped: 0

rewind_test (standalone, with fix):
  Total: 32  Passed: 22  Failed: 0  Skipped: 10
```

Matches audit claim verbatim.

## Hunt for missed shadow-propagation gaps

The audit's own assessment flagged that shadow propagation has produced
findings in 3 consecutive passes (V13 IncDecZ shadow, V14-NIT-01
prefix walk, V15-CPU-NIT-03 ULA+ shadow + V16-CPU-01 ULA+ load_state
push). The reviewer did a structural walk of every shadow field in
`ContentionModel` to look for a sibling gap.

### ContentionModel shadow inventory

| Field                          | Init() seed                  | NR write handler | load_state re-push | Notes |
|--------------------------------|------------------------------|------------------|--------------------|-------|
| `mem_active_page_`             | 0 (pwron default)            | per-cycle        | n/a (per-cycle)    | Pushed by FUSE callbacks; not state. |
| `cpu_speed_` (effective)       | `cfg.cpu_speed`              | NR 0x07          | 6530 ✓             | |
| `pending_cpu_speed_`           | implicit (set_cpu_speed)     | NR 0x07          | 6531 ✓             | |
| `contention_disable_` (eff)    | `set_contention_disable`     | NR 0x08 (b6)     | 6532 ✓             | Sourced from `mmu_.contention_disabled()`. |
| `contention_disable_shadow_`   | implicit (set_contention_disable) | NR 0x08    | 6532 ✓             | `set_contention_disable()` sets BOTH. |
| `port_7ffd_io_en_`             | NR 0x82 b1                   | NR 0x82          | 6533 ✓             | |
| `port_ulap_io_en_`             | NR 0x85 b0                   | NR 0x85          | **6534 ✓ (V16 fix)**| |
| `contended_slot_[4]`           | `is_contended_address()` mirror | 7FFD/1FFD/NR$03 | not re-pushed**   | **Benign** — only consumed at init line 288 to seed `mmu_.set_slot_contended()`; runtime path uses `mem_active_page_` per cycle, MMU's `slot_contended_[]` is the source of truth and IS serialised (mmu.cpp:848/917). |
| `type_`                        | `build()`                    | NR 0x03 commit   | rebuild_for_type 6527 ✓ | |
| `lut_[][]`                     | `build()`                    | NR 0x03 commit   | rebuild_for_type 6527 ✓ | Derived from type_. |

**`contended_slot_[]` desync after load_state — benign.** Verified by
exhaustive grep:

```
grep -rn "contended_slot_\|contention_\.is_contended_address" src/
```

The only non-`contention.{h,cpp}` consumer is `emulator.cpp:288`
(boot-time mirror to `mmu_.set_slot_contended()`). At runtime,
ContentionModel's per-cycle path (`contention_tick()`) consults only
`mem_active_page_`, NOT `contended_slot_[]`. The MMU side already
serialises `slot_contended_[4]`, so the live source of truth survives
load_state. `contention_.contended_slot_[]` ends up reset to
`{false, true, false, false}` (slot-1-only default per
`rebuild_for_type`) post-load, which would only matter if a future
caller re-mirrored these into MMU — no such caller exists. **Not a
finding** but worth noting in case a future refactor adds a runtime
consumer of `is_contended_address()`.

### Other Emulator subsystems

Spot-checked the load_state subsystem-by-subsystem ordering (lines
6427-6535). All NR-derived shadows in the contention path are now
covered. The `clock_` module owns its own `cpu_speed_/pending_cpu_speed_`
state and roundtrips it via its own save/load (clock.cpp:61-86). Other
NR-driven gates (e.g. divmmc rom3_active per V12 fix, spi flash_cs_enable,
i2c pi_i2c1_en) are explicitly re-pushed at the same load_state seam
(emulator.cpp:6486-6486 and earlier). No additional gaps surfaced.

### Audit's own "considered but not findings" sanity check

* **Pending cpu_speed shadow lossy snapshot.** Audit correctly notes
  that `nextreg_.regs_[0x07]` mirrors the *shadow* (immediate-on-write
  per VHDL :5786-5789) while the *effective* is bus-idle-gated. After
  load_state, both shadow and effective are seeded from `cached(0x07)`
  → both equal the post-snapshot shadow. If the snapshot was taken
  mid-commit-window, the shadow-vs-effective divergence is lost — but
  the snapshot never carried it (neither field is serialised). This is
  the same trade-off the V12-MEM-02 fix accepted for `port_7ffd_io_en`
  / `contention_disable`. **Architecturally consistent. Not a finding.**

* **`contention_disable_shadow_` not separately re-pushed.** Audit
  notes `set_contention_disable()` sets both shadow and effective from
  `mmu_.contention_disabled()`. Since MMU only serialises one bit
  (the shadow / immediate value), the effective is forced equal to the
  shadow post-load. Same accepted trade-off. **Not a finding.**

* **Stale `int_pending_` survival across frame boundary.** Audit
  worked through the unsigned subtraction in the pulse-expire check
  and concluded the scenario requires (a) INT request near frame end,
  (b) iff1=1 across boundary, (c) ULA scheduler firing near frame end
  (it fires near start). Logical chain holds. **Not a finding.**

All three correctly classified as architecturally-accepted lossiness
or unreachable scenarios.

## Build & test outputs (reviewer reproduction)

```
$ cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON
-- Configuring done (7.3s)
-- Generating done (0.1s)
-- Build files have been written to: .../build

$ cmake --build build -j$(nproc)
[100%] Built target jnext

$ ctest --test-dir build --output-on-failure
38/38 Test #38: contention_tests .................   Passed    0.03 sec
100% tests passed, 0 tests failed out of 38

$ ./build/test/fuse_z80_test build/test/fuse
=====================
Total: 1356  Passed: 1356  Failed:    0  Skipped:    0

$ ./build/test/rewind_test
Total:   32  Passed:   22  Failed:    0  Skipped:   10
```

All numbers match audit's reported figures.

## Convergence assessment

The audit observed (correctly) that the same shadow-propagation family
has produced findings in 3 consecutive passes (V13/V14/V15/V16). After
this fix, the `ContentionModel` shadow inventory is **fully covered**
by load_state for every runtime-used shadow (the `contended_slot_[]`
desync being benign as documented above).

**Reviewer recommendation:** the load_state contention re-push block
is now structurally complete. Pass-17 should still keep CPU in scope
per the audit's own recommendation, but the *load_state shadow
propagation* sub-family specifically may now be considered closed
unless a new shadow field is added (in which case the same pattern
should be applied at the same seam).

## Final return

```json
{
  "verdict": "APPROVE",
  "findings_verified": 1,
  "discriminative": [
    "rewind_test test_v16_cpu_01_load_state_repushes_port_ulap_io_en lines 374, 390 — PASS→FAIL on revert, PASS on restore"
  ],
  "issues": [],
  "missed_findings_fixed": [],
  "tests_passed": true,
  "head_sha": "ad0a85f4ad93e14b8ff1b36036561bb3b50bb8f0"
}
```
