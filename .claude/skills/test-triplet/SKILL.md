---
name: test-triplet
description: Run the full jnext test triplet (ctest unit-tests + FUSE Z80 opcodes + screenshot regression) and cache the result. Use when the user says "run the triplet", "full tests", "test everything", "run all tests", or asks for a comprehensive test status before committing/merging/version-bump.
---

# Test triplet

Run all three test layers and produce the canonical triplet line:

    unit N/N • FUSE 1356/1356 • regression P/F/S

This is the test posture that ends every jnext handover. Result is cached at `.claude/last-test-triplet.txt` so the SessionStart hook can display it next session.

**Expected counts are not restated here** — they go stale. The unit total is
pinned per suite in `test/unit-tests.conf` and the harness refuses to run if the
manifest and the registered suites disagree; the regression totals are pinned in
`test/00regression/regression_tests.conf` + `functional_tests.conf`. Take the
numbers from the run, and treat a changed total as a manifest question, not a
transcription question. FUSE is 1356 and does not move.

## Inputs

Optional: **worktree path** (default: repo root `/home/jorgegv/src/spectrum/jnext`).

## Steps (sequential — later layers depend on earlier ones)

### 0. Read the host load

```bash
nproc; cat /proc/loadavg; ps -eo pcpu,pid,comm --sort=-pcpu | head -5
```

Report it with the result. Contention manufactures failures, never passes: a
green run under load is *stronger* than one on an idle box, a red one costs one
re-run. Do not wait for a quiet machine.

### 1. Always rebuild — clean, gui-release

```bash
LANG=C make -C $TARGET clean && LANG=C make -C $TARGET gui-release
```

ABSOLUTE (`feedback_test_runs_always_rebuild`): every test run rebuilds first.
Never test an existing build — a stale binary gives false FAILs and false PASSes.
Before diagnosing any failure, check the binary's timestamp against `git log -1`.

### 2. Unit tests

```bash
LANG=C make -C $TARGET unit-test > /tmp/unit-$short.log 2>&1; echo "status=$?"
tail -25 /tmp/unit-$short.log
```

### 3. FUSE Z80 opcode suite

```bash
$TARGET/build/test/fuse_z80_test $TARGET/build/test/fuse > /tmp/fuse-$short.log 2>&1; echo "status=$?"
tail -3 /tmp/fuse-$short.log
```

### 4. Screenshot regression

```bash
short=$(git -C $TARGET rev-parse --short HEAD)
JNEXT_TEST_JOBS=4 bash $TARGET/test/00regression/regression.sh > /tmp/regression-$short.log 2>&1
echo "status=$?"
tail -30 /tmp/regression-$short.log
```

**Redirect every one of these — never pipe.** `| tail` and `| tee` hand you the
pipeline's exit status, so a failing `make` or a failing suite reads as success.
This is not hypothetical: a run printing `62 pass, 1 fail` and `UNIT TESTS FAILED`
in bold once went green in CI for exactly this reason
(`feedback_ci_runs_exact_local_commands`).

### 5. Cache

```bash
echo "unit N/N • FUSE 1356/1356 • regression P/F/S" > $TARGET/.claude/last-test-triplet.txt
```

If any layer has new FAILs vs baseline, append `(dirty)` so future SessionStart messages don't mislead.

## Report format

```
## Triplet
unit N/N • FUSE 1356/1356 • regression P/F/S

- branch: <branch> @ <short-sha>
- host load at run: <1min>/<5min>/<15min> on <nproc> cores
- logs: <paths>
- new failures: <list or "none">
```

## Hard rules per feedback memory

- `LANG=C` on every build/test command (`feedback_lang_c_builds`) — the shell
  locale is Spanish and greps for `warning:` otherwise match nothing.
- Clean gui-release rebuild first, always (`feedback_test_runs_always_rebuild`).
- Redirect, never pipe; check the status (`feedback_ci_runs_exact_local_commands`).
- Pre-existing skips ≠ regressions.
- Cache reset only on a fully-green run.
- Don't update reference screenshots without explicit user authorization.
- Don't fix anything — this skill reports, it doesn't fix.

## When to escalate to the `regression-runner` subagent

If the triplet is part of a larger flow (e.g. pre-merge check across many worktrees, or noisy environment), dispatch `regression-runner` instead — it surfaces only new failures, not pre-existing skips, and protects the main context from raw test output. This skill is for ad-hoc triplet runs.
