---
name: regression
description: Run the screenshot regression test suite and report the pass/fail/skip triplet against the baseline. Use when the user says "run the regression", "regression tests", "screenshot tests", or asks to confirm no rendering regressions before committing/merging.
---

# Regression test suite

Run the jnext screenshot + functional regression suite in `test/00regression/`.

**The row count is not restated here** — it goes stale. The pinned totals live in
`test/00regression/regression_tests.conf` (screenshots) and `functional_tests.conf`
(functional), and the harness itself refuses to run if the declared set and the
actual set disagree. Read the conf, or take the total from the run's own summary.

## Inputs

Optional: **worktree path** (default: `/home/jorgegv/src/spectrum/jnext`).

## Steps

### 0. Read the host load — before running, and before citing any result

```bash
nproc; cat /proc/loadavg; ps -eo pcpu,pid,comm --sort=-pcpu | head -5
```

Report the load number with the result so the claim is auditable. Your own agents
being idle says nothing about the box — VS Code, ansible and builds routinely
saturate it and are invisible from here.

**Load is asymmetric — this is the useful part.** Contention manufactures
*failures* (timeouts, audio underruns), never passes. So:

- A **green** run under load is *stronger* evidence than one on an idle box.
- A **red** run under load is inconclusive and costs exactly one re-run — never
  call it a "pre-existing failure".
- **Do not add waiting of your own.** A full run already waits, bounded
  (300 s), for the 1-minute load to fall below `nproc`, and only one full run
  per host runs at a time (GH #295) — beyond that, go ahead. (v0.99.86's
  regression passed 116/116 at load 10.87.)

`JNEXT_TEST_JOBS` bounds only the suite's own concurrency, not competing host
processes.

### 1. Always rebuild — clean, gui-release

```bash
LANG=C make -C $TARGET clean && LANG=C make -C $TARGET gui-release
```

Not "if needed", and not a conditional build dir. `regression.sh` picks up
whatever binary is sitting in the search path, so a stale one yields false FAILs
and — worse — false PASSes. A result that does not correspond to the code under
test is not a result.

### 2. Run the suite, redirecting to a log

```bash
short=$(git -C $TARGET rev-parse --short HEAD)
log=/tmp/regression-$short.log
LANG=C make -C $TARGET regression > "$log" 2>&1
status=$?
tail -30 "$log"
```

**Redirect — never pipe.** `| tee` and `| tail` both hand you the *pipeline's*
exit status, not the suite's, so a failing run reads as success. Capture to the
log (which `feedback_regression_log_to_file` requires anyway), then check
`$status` and read the file.

### 3. Parse Pass / Fail / Skip from the log tail.

### 4. Cache "P/F/S @ HEAD `<short>` on `<branch>`" to `$TARGET/.claude/last-test-triplet.txt` — the SessionStart hook reads it.

## Report format

```
## Regression
regression P/F/S on branch <branch> @ <short-sha>
Host load at run: <1min>/<5min>/<15min> on <nproc> cores
Log: <path>
New failures vs baseline: <list or "none">
```

## Hard rules per feedback memory

- `make regression` is canonical (`feedback_make_regression_canonical`).
- Redirect to a log file; never pipe a test command
  (`feedback_regression_log_to_file`, `feedback_ci_runs_exact_local_commands`).
- Clean gui-release rebuild before every run
  (`feedback_test_runs_always_rebuild`, `feedback_clean_gui_release_for_regression`).
- Report the host load; green-under-load is strong, red-under-load is one re-run
  (`feedback_measure_host_load_never_assume_quiet`).
- Pre-existing skips ≠ failures.
- **Never update reference screenshots without explicit user authorization**
  (`feedback_regression_refs`). Regenerating a reference to make the suite green
  destroys the only check that would have caught the change.
- `JNEXT_TEST_JOBS` is only a lane cap since GH #295: the real-time-bounded
  rows (`audio-underrun-func`, `screenshot-paused-func`, …) run in their own
  `quiet` phase. Check `make regression-stamp-check` first — a green stamp for
  the same non-doc content already satisfies the gate.
- Only a run with fail=0 AND skip=0 is stamped. If a full run's only FAILs are
  functional rows flagged as failing on a loaded host, it says PENDING:
  `make regression-confirm` re-runs exactly those rows solo and stamps the run
  if they pass. Any other FAIL or a SKIP is real — report it.
- Those two are examples, not the list (GH #245): rows that spawn short-lived
  processes have also failed under load and passed solo. A FAIL on a loaded
  host is unconfirmed until re-run SOLO (`make regression-rows ROWS=<row>`, which builds the binaries rows need;
  `make regression-confirm` after a full run whose only FAILs were loaded-host ones), and is not dismissed until that solo run passes. Always report the
  row name. The harness prints the load at start and end, marks each FAIL that
  happened with load ≥ `nproc`, and lists the failed rows after the results.
- Run in the branch worktree, not on main (`feedback_regression_in_branches`).

## When to escalate to the `regression-runner` subagent

If the user wants the full triplet (ctest + FUSE + regression) rather than just
the regression layer, use the `test-triplet` skill or dispatch the
`regression-runner` agent. This skill runs only the regression layer.
