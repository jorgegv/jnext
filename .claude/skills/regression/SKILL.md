---
name: regression
description: Run the screenshot regression test suite (33 cases) and report pass/fail/skip triplet against the baseline. Use when the user says "run the regression", "regression tests", "screenshot tests", or asks to confirm no rendering regressions before committing/merging.
---

# Regression test suite

Run the jnext screenshot regression suite. 33 end-to-end pixel-comparison tests in `test/00regression/`.

## Inputs

Optional:
- **Worktree path** (default: `/home/jorgegv/src/spectrum/jnext`).
- **Build dir** (default: `build/` — or `build/gui-release/` if the change touched GUI code; see `feedback_clean_gui_release_for_regression`).

## Steps

1. **Build if needed:**
   ```bash
   cmake --build $TARGET/build -j$(nproc) 2>&1 | tail -5
   ```

2. **Run regression with the canonical entry, teeing the log** (per `feedback_make_regression_canonical` + `feedback_regression_log_to_file`):
   ```bash
   short=$(git -C $TARGET rev-parse --short HEAD)
   log=/tmp/regression-$short.log
   bash $TARGET/test/00regression/regression.sh 2>&1 | tee $log
   ```

3. **Parse the tail** for Pass / Fail / Skip counts.

4. **Cache** "P/F/S @ HEAD <short> on <branch>" to `$TARGET/.claude/last-test-triplet.txt`. The SessionStart hook reads this.

## Report format

```
## Regression
regression P/F/S on branch <branch> @ <short-sha>
Log: <path>
New failures vs baseline (33/0/0): <list or "none">
```

## Hard rules per feedback memory

- `make regression` is canonical (`feedback_make_regression_canonical`).
- Log to file (`feedback_regression_log_to_file`).
- Pre-existing skips ≠ failures.
- **Never update reference screenshots without explicit user authorization** (`feedback_pixel_equivalence_for_ref_regen`).
- For GUI-touching changes, use `build/gui-release/` (`feedback_clean_gui_release_for_regression`).
- Set `JNEXT_TEST_JOBS=4` on every regression invocation (`feedback_jnext_test_jobs`). Never raise it for speed — `audio-underrun-func` and `screenshot-paused-func` are real-time-bounded and fail under CPU contention.
- Run regression in the branch worktree, not on main (`feedback_regression_in_branches`).

## When to escalate to the `regression-runner` subagent

If the user wants the full triplet (ctest + FUSE + regression) rather than just the regression layer, use the `test-triplet` skill or dispatch the `regression-runner` agent. This skill runs only the regression layer.
