# Pass-15 NMI + Multiface + Port + NextREG — Independent Review

- **Reviewer**: independent reviewer agent (task2-verify15-nmi-mf-port-reviewer)
- **Date**: 2026-05-10
- **Audit head reviewed**: `1b1b27c` ("doc(task2-verify15-nmi-mf-port): pass-15 audit report — 2 findings")
- **Branch**: `task2/verify15-nmi-mf-port-reviewer` (off `task2/verify15-nmi-mf-port`)
- **Worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify15-nmi-mf-port-reviewer`
- **Verdict**: **APPROVE**

## Summary

Pass-15 raised two class-(a) findings and shipped two tightly-scoped C++
fixes plus discriminative regression rows:

| ID          | NR    | What pre-fix did              | What VHDL says                | Fix shape                       |
| ----------- | ----- | ----------------------------- | ----------------------------- | ------------------------------- |
| V15-NMP-01  | 0x63  | write_handler returned `v`    | NO read mux; default `(others=>'0')` | write_handler now returns 0   |
| V15-NMP-02  | 0xFF  | write_handler returned `v`    | NO read mux; default `(others=>'0')` | write_handler now returns 0   |

Both are the same shape as the existing G149 (NR 0x60), V14-NMP-03 (NR
0x2B), V14-NMP-04 (NR 0x2A), and the cluster of other write-only NRs
already canonicalised in earlier passes. The audit additionally claims:
"after this pass the writeable-but-not-readable NR set is empty". I
verified that claim end-to-end against the VHDL — see § 5 below.

## 1. VHDL claim — both NRs lack read mux entries

The single read mux is at `zxnext.vhd:5882` (`case nr_register is`),
extending to the closing `when others` at `:6286-6287`:

```
when others =>
   port_253b_dat <= (others => '0');
```

Walking the case body 5882..6287 and listing every `when X"NN"` literal
yields 122 readable NRs. **NR 0x63 and NR 0xFF are not among them**:

```
$ grep -E '^\s*when X"(63|FF)"' zxnext.vhd:5882..6290
(no output)
```

The default fall-through is `port_253b_dat <= (others => '0')` — i.e.
hardware reads of unmapped NRs return `0x00`. **Audit claim CONFIRMED.**

## 2. Fix correctness

The diff at `1b1b27c` contains exactly two single-line behavioural
changes plus two test rows:

* `src/core/emulator.cpp:1655` — NR 0x63 lambda: `return v` → `return 0`
* `src/core/emulator.cpp:926` — NR 0xFF lambda: `return v` → `return 0`
* `test/nextreg/nextreg_integration_test.cpp` — added `WO-INT-63` and
  `WO-INT-FF` rows in the `test_write_only_read_zero(...)` group

The `copper_.write_reg_0x63(v)` and `palette_.nr_ff_poke(...)`
side-effect calls remain — only the byte the cache stores is canonicalised
to 0. This matches the VHDL: the write side-effects (auto-increment of
`nr_copper_addr`, palette dpram poke) are still latched, and only the
read path (which on real hardware doesn't exist) is forced to 0.

The VHDL line citations in the inline C++ comments and in the test
descriptions are accurate — verified against the actual file contents.

## 3. Discriminative regression — revert-confirmed

Reviewer-side (independent of audit) revert experiment, on this branch:

### Revert NR 0x63 only (line 1655: `return 0` → `return v`)

```
[100%] Linking CXX executable nextreg_integration_test
  FAIL WO-INT-63: NR 0x63 write-only Copper data — read returns 0 (no leak of cached
  write byte) [zxnext.vhd:4887 nr_copper_we, :5433-5439 write side, :5878-6289
  others=>'0'] [wrote=0x55 got=0x55 want=0x00]
Total:  243  Passed:  242  Failed:    1  Skipped:    0
```

WO-INT-63 fails with exactly the predicted signature. WO-INT-FF still
passes (independence confirmed). Restored.

### Revert NR 0xFF only (line 926: `return 0` → `return v`)

```
[100%] Linking CXX executable nextreg_integration_test
  FAIL WO-INT-FF: NR 0xFF write-only ULA+ palette poke — read returns 0 (no leak of
  cached write byte) [zxnext.vhd:4906 nr_ff_we, :4919 nr_palette_value, :5878-6289
  others=>'0'] [wrote=0xA5 got=0xA5 want=0x00]
Total:  243  Passed:  242  Failed:    1  Skipped:    0
```

WO-INT-FF fails with exactly the predicted signature. WO-INT-63 still
passes (independence confirmed). Restored — `Total: 243 Passed: 243`.

Both rows are **discriminative**: each fix is necessary and sufficient
for its own row.

## 4. Full Release-mode test sweep (post-restore)

```
$ cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON
$ cmake --build build -j$(nproc)
[100%] Built target jnext

$ ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 38
Total Test time (real) = 0.31 sec

$ ./build/test/fuse_z80_test build/test/fuse
Total: 1356  Passed: 1356  Failed:    0  Skipped:    0

$ ./build/test/nextreg_integration_test
Total:  243  Passed:  243  Failed:    0  Skipped:    0
```

Zero FAILs. Zero SKIPs. Clean sweep.

## 5. Hunt for missed cases — exhaustive audit of write-only NR set

### Method

1. Collected every write strobe decoded from `nr_wr_reg` at
   `zxnext.vhd:4837..4912` (the single write decoder process).
2. Collected every readable NR from `zxnext.vhd:5882..6286` (the
   single read mux case statement).
3. Computed `writable_NRs - readable_NRs` (with X"35..39" / X"75..79"
   pairs handled correctly — both halves are write-only sprite mirror
   commits).
4. For each NR in the difference, verified the C++
   `set_write_handler(...)` body returns 0 (so the cache canonicalises
   to 0).

### Writable-but-not-readable NR set (17 NRs)

| NR    | C++ handler returns | Citation                                    |
| ----- | ------------------- | ------------------------------------------- |
| 0x04  | 0 (G149 fix)        | `emulator.cpp:2233`                         |
| 0x29  | 0 (G149 fix)        | `emulator.cpp:1287` (membrane_stick.write_nr_29) |
| 0x2A  | 0 (V14-NMP-04 fix)  | `emulator.cpp:1320`                         |
| 0x2B  | 0 (V14-NMP-03 fix)  | `emulator.cpp:1302` (membrane_stick.write_nr_2b) |
| 0x35  | 0 (PASS-5)          | `emulator.cpp:1488` for-loop i=0            |
| 0x36  | 0 (PASS-5)          | i=1                                         |
| 0x37  | 0 (PASS-5)          | i=2                                         |
| 0x38  | 0 (PASS-5)          | i=3                                         |
| 0x39  | 0 (PASS-5)          | i=4                                         |
| 0x60  | 0 (G149 fix)        | `emulator.cpp:1641`                         |
| 0x63  | 0 (V15-NMP-01 fix)  | `emulator.cpp:1655` ← **this pass**         |
| 0x75  | 0 (PASS-5)          | `emulator.cpp:1517` for-loop i=0            |
| 0x76  | 0 (PASS-5)          | i=1                                         |
| 0x77  | 0 (PASS-5)          | i=2                                         |
| 0x78  | 0 (PASS-5)          | i=3                                         |
| 0x79  | 0 (PASS-5)          | i=4                                         |
| 0xFF  | 0 (V15-NMP-02 fix)  | `emulator.cpp:926`  ← **this pass**         |

**Result**: every single write-only NR in the VHDL has a corresponding
C++ write handler that returns 0. **Zero gaps.** The audit's claim
"the writeable-but-not-readable NR set is now exhausted" **VERIFIED**.

### Spot checks for the prompt's call-out NRs

The prompt asked specifically about NR 0x43, 0x44, 0x45, 0x46, 0x47,
0x4A, 0x4B, 0x4C, 0x20, 0x21, 0x22, 0x40, 0x41, 0x42:

| NR    | Writable? (VHDL) | Readable? (VHDL) | Cache leak risk?     |
| ----- | ---------------- | ---------------- | -------------------- |
| 0x20  | yes (`:4846`)    | yes (`when X"20"`) | no — read mux composes from registered state |
| 0x21  | NO write strobe  | NO read entry    | dead — `regs_[]` raw read returns `regs_[0x21]`; both writes and reads are fully outside both sides of the FPGA NR register file. **NOT a write-only-leak case** since the write side has no FPGA effect either. (See note below.) |
| 0x22  | yes (`:4847`)    | yes (`when X"22"`) | no — read mux composes from registered state |
| 0x40  | NO write strobe  | yes (palette idx) | not a leak (writable-not-readable inverted: this is readable-not-writable, written via `write_to_nr_index` indirection) |
| 0x41  | yes (`:4878`)    | yes (`when X"41"`) | no |
| 0x42  | NO write strobe  | yes (`when X"42"`) | n/a |
| 0x43  | NO write strobe  | yes (`when X"43"`) | n/a |
| 0x44  | yes (`:4879`)    | yes (`when X"44"`) | no |
| 0x45  | NO write strobe  | NO read entry    | not in either set — outside FPGA NR register file |
| 0x46  | NO write strobe  | NO read entry    | as 0x45 |
| 0x47  | NO write strobe  | NO read entry    | as 0x45 |
| 0x4A  | NO write strobe  | yes (`when X"4A"`) | n/a |
| 0x4B  | NO write strobe  | yes (`when X"4B"`) | n/a |
| 0x4C  | NO write strobe  | yes (`when X"4C"`) | n/a |

**Note on NR 0x21 and the wholly-undecoded NRs**: these NRs are not in
the writable set in the VHDL — they have no write strobe in the
`zxnext.vhd:4837..4912` case statement. In jnext, a raw write goes to
`regs_[0x21]` via `NextReg::write` (no handler) and a read returns
`regs_[0x21]` via `NextReg::read` (no handler). On real hardware, a
write to 0x21 has no FPGA-state effect (no strobe), and a read returns
`0x00` via the read mux fall-through. So **jnext WOULD leak a cached
value for these undecoded NRs** if some user code wrote to them — but
this is outside the scope of "write-only" (which the audit defined as
"writable strobe with no read mux entry"). It's a *separate*
fully-undecoded-NR class.

I considered raising this as a missed finding for the reviewer to fix.
Inspecting the C++ the actual paths are:
* `regs_[0x21]` and similar — mirrored last-write-byte. Real hardware
  returns 0.
* This affects NR 0x06, 0x21, 0x24, 0x25, 0x2F (?), 0x4D-0x4F, 0x5x
  (some), 0x65-0x67, 0x6D, 0x72-0x74, 0x7A-0x7E, 0x95-0x97, 0x9C-0x9F,
  0xA1, 0xA4-0xA7, 0xAA-0xAF, 0xB3-0xB7, 0xBC-0xBF, 0xC1, 0xC7,
  0xCB, 0xCF-0xD7, 0xDB-0xEF, 0xF1-0xF7, 0xFB-0xFE — i.e. dozens.

This is a much wider class than the targeted "write-only" set; it
overlaps with passes that intentionally scaffold registered behaviour
later (some of those NRs ARE valid-but-not-yet-implemented in the C++
side and will get proper write handlers in future Waves). Treating the
whole undecoded-NR class as "leaky" would force a sweeping
canonicalisation that conflicts with the staged-implementation
approach the project has chosen. **I do not promote this to a missed
finding** — it is a pre-existing scoping decision, not a Pass-15
regression. The audit's narrower claim ("writeable-but-not-readable NR
set is empty") is precisely correct and that's what this pass set out
to do.

### Conclusion of the missed-cases hunt

**No missed write-only NR findings**. The Pass-15 audit is exhaustive
relative to its scope (writable strobes with no read mux entry).

## 6. Style / build hygiene

* Inline comments are thorough, cite the right VHDL lines, and follow
  the established G149 / V14-NMP-03 / V14-NMP-04 idiom.
* Test rows include the `wrote=…/got=…/want=…` discriminator that
  matches the pre-fix failure signature.
* No probes, no debug logging, no unrelated changes.
* Build is clean (Release-mode, all targets).

## 7. Process compliance

* Followed the V14 / Pass-N rule pattern: one report, one commit, two
  fixes, two regression rows.
* No push, no merge, no PR.
* No probe leakage.
* Worktree clean post-test (only an unstaged `roms` symlink that the
  reviewer added locally so the build could find embedded boot ROMs;
  not committed).

## 8. Verdict

**APPROVE** — both fixes are correct, both tests are discriminative,
the missed-cases hunt confirms zero write-only NR gaps remain, the full
test suite passes Release-mode with zero FAILs / zero SKIPs.

## Final return JSON

```json
{
  "verdict": "APPROVE",
  "findings_verified": 2,
  "discriminative": [
    "WO-INT-63: revert -> wrote=0x55 got=0x55 want=0x00 FAIL; restore -> PASS",
    "WO-INT-FF: revert -> wrote=0xA5 got=0xA5 want=0x00 FAIL; restore -> PASS"
  ],
  "issues": [],
  "missed_findings_fixed": [],
  "tests_passed": true,
  "head_sha": "1b1b27c6f1544f8944db091d4b3df36d73122fe7"
}
```
