# Testing posture

Three test layers. VHDL is the single oracle. No-fail policy on each layer.

## The three layers

| Layer | Tool | Count | Notes |
|---|---|---|---|
| Subsystem unit tests | `LANG=C make unit-test` (ctest) | 38 | Per-subsystem deep tests |
| FUSE Z80 opcode suite | `./build/test/fuse_z80_test build/test/fuse` | 1356 | Z80 instruction-level conformance |
| Screenshot regression | `bash test/00regression/regression.sh` | 33 | End-to-end pixel comparison |

The triplet **N/N • 1356/1356 • 33/0/0** ends every handover.

## Canonical commands

```bash
LANG=C make unit-test                           # ctest
./build/test/fuse_z80_test build/test/fuse      # FUSE
bash test/00regression/regression.sh            # regression (use `make regression`)
```

Per feedback memory:

- **`LANG=C` is mandatory** on the unit-test build path (`feedback_lang_c_builds`).
- **`make regression` is the canonical entry** (`feedback_make_regression_canonical`).
- **Tee regression to a log file** so reviewers can read it (`feedback_regression_log_to_file`).
- **For GUI-touching changes, build `build/gui-release/` first** (`feedback_clean_gui_release_for_regression`).
- **Set `JNEXT_TEST_JOBS=1`** when running parallel work alongside regression (`feedback_jnext_test_jobs`).
- **Run regression in the branch worktree, NOT on main** (`feedback_regression_in_branches`).
- **Run regression on main only as a final convergence check** (`feedback_regression_main_session`).

## VHDL-as-oracle

`doc/testing/UNIT-TEST-PLAN-EXECUTION.md` is the authoritative process doc.
Highlights:

- **The VHDL at `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/` is the oracle.** Not CSpect. Not Fuse. Not ZEsarUX. Not the wiki.
  (`feedback_vhdl_faithful_only`, `feedback_zesarux_baseline`)
- **Tests are written from VHDL spec, not from observed emulator behavior.**
  Test the spec, not the current implementation. (`feedback_test_from_vhdl`)
- **Pass / Fail / Skip distinction is meaningful.** SKIP means "intentionally
  deferred per a documented escalation"; not "I gave up". Skips must have a
  reduction procedure documented. (`feedback_skip_reduction_procedure`)
- **1:1:1 process** — every emulator fix ships with a discriminative test that
  was previously skipped or absent, in the same commit, with independent
  review.
- **No unobservable tests.** A test that doesn't exercise observable Z80-visible
  state is theatre. (`feedback_unobservable_audit_rule`)
- **Fixtures spanning files** — when test fixtures touch multiple files, the
  audit must cover all of them. (`feedback_audit_test_fixtures_across_files`)
- **Uniform test output.** Tests format their output consistently across
  subsystems. (`feedback_uniform_test_output`)

## Reference screenshots

The 33 regression tests compare emulator output to PNG references in
`test/img/`. When references need updating:

- **Pixel-equivalence first.** If a "new" reference differs from the old one
  by exact equality (no rendering change), regenerate with confidence. If
  pixels DO differ, the regen requires an explicit user authorization plus
  pixel-equivalence justification (what changed, why it's intentional, what's
  observed). (`feedback_pixel_equivalence_for_ref_regen`)
- **Use the canonical regen entry:** `bash test/00regression/generate-references.sh`.
- **Never regen during a fix session.** Regen is a separate, deliberate act.

## Skip-reduction procedure

Per `feedback_skip_reduction_procedure`:

1. Pick a skipped test.
2. Read the VHDL for what it's testing.
3. Write the discriminative test (fails on current emulator).
4. Fix the emulator (in the same commit OR a follow-up commit, but always
   paired with the test).
5. Run unit-test + regression to confirm.
6. Independent reviewer approves.

This is the per-fix workflow that drove Task 2's 25 audit passes.

## Test-coverage waves

When a subsystem is converged via audits, a separate "test-coverage wave"
adds discriminative regression tests for every fix. These tests are
**retroactive** — they protect against regression of fixes that didn't ship
with a test the first time. Task 2's Pass-10 ran this wave for 4 subsystems
(memory, divmmc, NMI, CPU).

## Surfaced regressions during a phase

Per `feedback_surfaced_regressions_in_phase`: if a test that has been passing
suddenly fails during an unrelated change, that's a regression — not a
new test-design issue. Investigate the regression first.

## Test plan audits

The traceability matrix at `doc/testing/TRACEABILITY-MATRIX.md` is the source
of truth for what's covered. Refresh via `perl test/refresh-traceability-matrix.pl`. Per `feedback_audit_passing_rows`, when auditing
the matrix, examine the passing rows too — not just the failing/skipped ones.
